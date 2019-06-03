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
#include <unistd.h>

#include <netdb.h>

#include <sys/socket.h>
#include <netinet/tcp.h>

#include <poll.h>

/*
 * this tool listens on a specified (ipv4/ipv6) address for multiple clients
 * and retransmits what each client sends (TCP payload) to all other clients
 */

#define PROGNAME "tcp_repeater"

/* used for repeating to other clients */
#define BUFFSIZ 65536

void perror_down(char *msg)
{
    if (msg)
        perror(msg);
    exit(1);
}
void error_down(char *msg)
{
    if (msg)
        fprintf(stderr, "%s\n", msg);
    exit(1);
}
int set_user_timeout(int s, int val)
{
#ifdef TCP_USER_TIMEOUT
    return setsockopt(s, IPPROTO_TCP, TCP_USER_TIMEOUT, &val, sizeof(val));
#else
    return 0;
#endif
}


/* re-use poll array of struct pollfd to keep track of client fds */


int addclient(struct pollfd **polls, int *polls_num, int newclient)
{
    struct pollfd *newpolls;
    int newpolls_num;
    int i;

    if (set_user_timeout(newclient, 10) == -1)
        return -1;

    /* first look for "removed" client, -1 in fd, use it if found */
    for (i = 1; i < *polls_num; i++) {
        if ((*polls)[i].fd == -1) {
            (*polls)[i].fd = newclient;
            (*polls)[i].events = POLLIN | POLLPRI;
            return i;
        }
    }

    /* all current "slots" used, allocate a new one */
    newpolls_num = *polls_num + 1;
    newpolls = realloc(*polls, newpolls_num*sizeof(struct pollfd));
    if (newpolls == NULL)
        return -1;

    newpolls[newpolls_num-1].fd = newclient;
    newpolls[newpolls_num-1].events = POLLIN;

    *polls_num = newpolls_num;
    *polls = newpolls;

    return newpolls_num-1;
}

int delclient(struct pollfd **polls, int *polls_num, int oldclient)
{
    int i;

    close(oldclient);

    for (i = 1; i < *polls_num; i++) {
        if ((*polls)[i].fd == oldclient) {
            (*polls)[i].fd = -1;
            (*polls)[i].events = 0;
            (*polls)[i].revents = 0;
            return i;
        }
    }

    return -1;
}

void sendall(struct pollfd **polls, int *polls_num, int from,
             char *buff, int bufflen)
{
    int i;

    for (i = 1; i < *polls_num; i++) {
        if ((*polls)[i].fd == -1 || (*polls)[i].fd == from)
            continue;
        if (send((*polls)[i].fd, buff, bufflen, MSG_NOSIGNAL) <= 0)
            delclient(polls, polls_num, from);
    }
}


int main(int argc, char **argv)
{
    int fd;
    struct addrinfo *ainfo, hints;

    int i, rc;
    ssize_t bytes;

    struct pollfd *fds;
    int nfds;

    char buff[BUFFSIZ];

    if (argc < 3)
        error_down("Usage: " PROGNAME " <listen_addr> <listen_port>");

    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;

    /* resolve listen_addr and listen_port */
    rc = getaddrinfo(argv[1], argv[2], &hints, &ainfo);
    if (rc != 0)
        error_down((char*)gai_strerror(rc));

    /* create stream socket */
    fd = socket(ainfo->ai_family, ainfo->ai_socktype, ainfo->ai_protocol);
    if (fd < 0)
        perror_down("socket");

    /* use SO_REUSEADDR */
    rc = 1;
    if (setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &rc, sizeof(rc)) == -1) {
        perror_down("setsockopt");
    }

    /* bind the socket to address/port */
    if (bind(fd, ainfo->ai_addr, ainfo->ai_addrlen) == -1)
        perror_down("bind");

    freeaddrinfo(ainfo);

    /* listen (backlog of 8 connections) */
    if (listen(fd, 8) == -1)
        perror_down("listen");

    /* add listening socket */
    nfds = 1;
    fds = malloc(nfds*sizeof(struct pollfd));
    fds[0].fd = fd;
    fds[0].events = POLLIN | POLLPRI;

    /* poll here */

    /* accept a client, add it to queue */
    while (1) {
        if (poll(fds, nfds, -1) == -1)
            perror_down("poll");

        /* special case: listening socket, needs accept */
        if (fds[0].revents & (POLLIN | POLLPRI)) {
            rc = accept(fd, NULL, NULL);
            if (rc != -1)
                addclient(&fds, &nfds, rc);
            continue;
        }

        for (i = 1; i < nfds; i++) {
            if (fds[i].revents & (POLLIN | POLLPRI)) {
                bytes = read(fds[i].fd, buff, sizeof(buff));
                if (bytes <= 0) {
                    delclient(&fds, &nfds, fds[i].fd);
                    continue;
                }
                sendall(&fds, &nfds, fds[i].fd, buff, bytes);
            }
        }
    }

    close(fd);

    return 0;
}
