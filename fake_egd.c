/*
 * Copyright (c) 2015 Red Hat, Inc. All rights reserved.
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program. If not, see <http://www.gnu.org/licenses/>.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <errno.h>
#include <unistd.h>
#include <fcntl.h>

#include <netdb.h>
#include <poll.h>

#include <sys/socket.h>
#include <sys/wait.h>

/*
 * fake entropy generation (non-)daemon
 *
 * this tool serves a certain device (/dev/urandom by default) or file/pipe/etc.
 * using TCP streams to multiple clients, speaking the EGD protocol
 * (defined and primarily used by http://egd.sourceforge.net/)
 *
 * the primary motivation is to work around deliberate limitations of random
 * device names imposed by libvirt/qemu (which allow only /dev/{random,hwrng}),
 * allowing experienced sysadmins to use /dev/urandom where appropriate
 */

#define PROGNAME "fake_egd"

/* maximum request "packet" size - limited by protocol to 1 byte of data
 * + some control bytes */
#define REQSIZE 300

/* random source (typically char device, but can be file/fifo) */
char *rnd_dev = "/dev/urandom";


void perror_down(char *msg)
{
    if (msg)
        perror(msg);
    exit(EXIT_FAILURE);
}
void error_down(char *msg)
{
    if (msg)
        fprintf(stderr, "%s\n", msg);
    exit(EXIT_FAILURE);
}


/* read exactly count bytes */
ssize_t exread(int fd, void *buf, size_t count)
{
    ssize_t requested = count, got;
    while (requested > 0) {
        got = read(fd, buf, requested);
        if (got <= 0)  /* end on eof */
            return got;
        requested -= got;
    }
    return count;
}
int process_one_req(int client)
{
    char buff[REQSIZE];
    int dev;
    ssize_t bytes;
    int wanted;

    /* read op */
    if (exread(client, buff, 1) <= 0)
        return 0;

    switch (buff[0]) {
        case 0x00:
            /* "get entropy level" - wtf? nr. of bytes available? */
            /* "  0xMM (msb) 0xmm 0xll 0xLL (lsb)" */
            /* in theory, we should return 0xff in all 4 bytes, but some clients
             * may be broken and expect lower values, so return 2^15-1, LE */
            write(client, "\x00\x00\x7f\xff", 4);
            break;

        case 0x01:
            /* "read entropy nonblocking" ;; "0xNN (bytes requested)" */
            /* "  0xMM (bytes granted) MM bytes" */
            if (exread(client, buff+1, 1) <= 0)
                return 0;
            dev = open(rnd_dev, O_RDONLY | O_NONBLOCK);
            if (dev == -1)
                break;
            bytes = read(dev, buff+1, (uint8_t)buff[1]);
            if (bytes == -1) {
                if (errno == EAGAIN)
                    bytes = 0;  /* would block = nothing read */
                else
                    break;
            }
            buff[0] = (char)bytes;
            write(client, buff, 1+bytes);
            close(dev);
            break;

        case 0x02:
            /* "read entropy blocking" ;; "0xNN (bytes desired)" */
            /* "  [block] NN bytes" */
            if (exread(client, buff+1, 1) <= 0)
                return 0;
            dev = open(rnd_dev, O_RDONLY);
            if (dev == -1)
                break;
            /* needs a loop, may return less than wanted (ie. reading fifo) */
            wanted = (uint8_t)buff[1];
            while (wanted > 0) {
                bytes = read(dev, buff, wanted);
                if (bytes == -1)
                    break;
                write(client, buff, bytes);
                wanted -= bytes;
            }
            close(dev);
            break;

        case 0x03:
            /* "write entropy" ;;
             *   "0xMM 0xLL (bits of entropy) 0xNN (bytes of data) NN bytes" */
            /* we fake this one - read the request, but throw it away */
            if (exread(client, buff+1, 3) <= 0)
                return 0;
            if (exread(client, buff+4, (uint8_t)buff[3]) <= 0)
                return 0;
            break;

        case 0x04:
            /* "report PID" */
            /* "  0xMM (length of PID string, not null-terminated) MM chars" */
            snprintf(buff+1, 64, "%u", getppid());
            buff[0] = strlen(buff+1);
            write(client, buff, 1+(uint8_t)buff[0]);
            break;

        default:
            /* unknown - can't skip, so fail */
            return 0;
    }

    /* success without error/eof */
    return 1;
}

int main(int argc, char **argv)
{
    int fd;
    struct addrinfo *ainfo, hints;
    ssize_t rc;
    struct pollfd pfd;

    if (argc < 3)
        error_down("Usage: " PROGNAME " <listen_addr> <listen_port> [rnd_dev]");
    if (argc >= 4)
        rnd_dev = argv[3];

    /* initialize listening sock */
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;

    rc = getaddrinfo(argv[1], argv[2], &hints, &ainfo);
    if (rc != 0)
        error_down((char*)gai_strerror(rc));

    fd = socket(ainfo->ai_family, ainfo->ai_socktype, ainfo->ai_protocol);
    if (fd < 0)
        perror_down("socket");

    rc = 1;
    if (setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &rc, sizeof(rc)) == -1) {
        perror_down("setsockopt");
    }

    if (bind(fd, ainfo->ai_addr, ainfo->ai_addrlen) == -1)
        perror_down("bind");

    freeaddrinfo(ainfo);

    if (listen(fd, 8) == -1)
        perror_down("listen");

    /* process requests, forked model */
    pfd.fd = fd;
    pfd.events = POLLIN | POLLPRI;
    while (1) {
        if (poll(&pfd, 1, 100) == -1)
            if (errno != EINTR)
                perror_down("poll");

        if (pfd.revents & (POLLIN | POLLPRI)) {
            rc = accept(fd, NULL, NULL);
            if (rc == -1)
                continue;
    
            switch (fork()) {
                case -1:
                    perror_down("fork");
                case 0:
                    /* child - read+parse request */
                    close(fd);
                    fd = rc;
                    while (process_one_req(fd)) {} /* until eof */
                    close(fd);
                    exit(EXIT_SUCCESS);
                default:
                    close(rc);
                    break;
            }
        }

        /* housekeeping: collect all zombies */
        while (waitpid(-1, NULL, WNOHANG) > 0);
    }

    close(fd);

    return 0;
}
