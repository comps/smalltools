/*
 * Copyright (c) 2017 Red Hat, Inc. All rights reserved.
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

/*
 * this tool listens on specified port(s) or port ranges, accepting multiple
 * clients on each port, and re-transmitting (broadcasting) any incoming data
 * to all connected clients on a given port
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <inttypes.h>
#include <stdarg.h>
#include <stdbool.h>
#include <errno.h>
#include <limits.h>
#include <fcntl.h>
#include <unistd.h>

#include <netdb.h>
#include <sys/socket.h>
#include <netinet/tcp.h>

#include <pwd.h>
#include <grp.h>

#include <sys/resource.h>
#include <sys/epoll.h>

/*
 * minimum guaranteed number of clients connected at any given time
 * - this is used only for setrlimit() (max. opened file descriptors)
 */
#define MIN_CLIENTS 2000

/*
 * unprivileged user, switch to it (if possible) after opening listen sockets
 */
#define UNPRIVILEGED_USER "nobody"
#define UNPRIVILEGED_GROUP "nobody"

void err(const char *fmt, ...)
{
    va_list ap;
    if (fmt) {
        va_start(ap, fmt);
        fprintf(stderr, "error: ");
        vfprintf(stderr, fmt, ap);
        fprintf(stderr, "\n");
        va_end(ap);
    }
}
void fatal(const char *fmt, ...)
{
    va_list ap;
    if (fmt) {
        va_start(ap, fmt);
        fprintf(stderr, "error: ");
        vfprintf(stderr, fmt, ap);
        fprintf(stderr, "\n");
        va_end(ap);
    }
    exit(EXIT_FAILURE);
}
void pfatal(const char *msg)
{
    perror(msg);
    exit(EXIT_FAILURE);
}

/*
 * epoll can store either fd or user data, noth both, which is why we use
 * epoll_fd_data to store both
 *
 * as user data, bcast_data is used (which is just a fancy dynamic list
 * with list-specific metadata in a struct), holding array of epoll_fd_data
 * of "related" fds that share the same broadcast domain
 * - we don't store fd ints directly because epoll_fd_data are malloc'd and
 *   we need to free them when closing fds that fail send()
 *
 * bcast_data are shared,
 * - listening sockets should add a new client fd to it,
 * - connected sockets should re-send incoming data to all fds in it
 * the same bcast_data instance is shared between listening/connected epoll
 * structs using the same service/port
 */
struct epoll_fd_data {
    bool listening;
    int fd;
    void *data;
};
struct bcast_data {
    int listenfd;
    /* how many valid (!= -1) fds are stored */
    size_t used;
    /* maximum fds count ever reached when storing a fd */
    size_t used_max;
    /* total allocated size for the fds array (max possible fds count) */
    size_t max;
    struct epoll_fd_data **fds;
};

int set_nonblocking(int sock)
{
    int flags;
    if ((flags = fcntl(sock, F_GETFL, 0) == -1))
        flags = 0;
    return fcntl(sock, F_SETFL, flags | O_NONBLOCK);
}

int create_listen_socket(long port)
{
    int fd, rc;
    struct addrinfo *ainfo, hints;
    char service[6];

    snprintf(service, sizeof(service), "%ld", port);

    memset(&hints, 0, sizeof(hints));
    //hints.ai_family = AF_INET6;
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_flags = AI_NUMERICSERV | AI_PASSIVE;

    rc = getaddrinfo(NULL, service, &hints, &ainfo);
    if (rc != 0)
        fatal((char*)gai_strerror(rc));

    fd = socket(ainfo->ai_family, ainfo->ai_socktype, ainfo->ai_protocol);
    if (fd < 0)
        pfatal("socket");

    //rc = 0;
    //if (setsockopt(fd, IPPROTO_IPV6, IPV6_V6ONLY, &rc, sizeof(rc)) == -1)
    //    pfatal("setsockopt(IPPROTO_IPV6)");

    rc = 1;
    if (setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &rc, sizeof(rc)) == -1)
        pfatal("setsockopt(SO_REUSEADDR)");

    if (set_nonblocking(fd) == -1)
        pfatal("set_nonblocking");

    if (bind(fd, ainfo->ai_addr, ainfo->ai_addrlen) == -1)
        pfatal("bind");

    freeaddrinfo(ainfo);

    if (listen(fd, 8) == -1)
        pfatal("listen");

    return fd;
}

void raise_nofile_rlimit(rlim_t to)
{
    struct rlimit rlim;

    if (getrlimit(RLIMIT_NOFILE, &rlim) == -1)
        pfatal("getrlimit(RLIMIT_NOFILE)");

    if (rlim.rlim_cur < to) {
        rlim.rlim_max = rlim.rlim_cur = to;
        if (setrlimit(RLIMIT_NOFILE, &rlim) == -1)
            pfatal("setrlimit(RLIMIT_NOFILE)");
    }
}

void drop_privileges(const char *user, const char *group)
{
    struct group *grp = getgrnam(group);
    struct passwd *pwd = getpwnam(user);

    if (grp && pwd) {
        if (setregid(grp->gr_gid, grp->gr_gid) == -1 && errno != EPERM)
            pfatal("setregid");
        if (setreuid(pwd->pw_uid, pwd->pw_uid) == -1 && errno != EPERM)
            pfatal("setreuid");
    } else {
        err("user %s or group %s not found", user, group);
    }
}

void cli_parse_range(const char *str, long *from, long *to)
{
    char *second;

    long res;
    char *end;

    res = strtol(str, &end, 10);
    if (errno)
        pfatal("strtol");
    if (res > USHRT_MAX || end == str || (*end != '-' && *end != '\0'))
        fatal("invalid port range: %s", str);
    *from = res;

    if ((second = strchr(str, '-'))) {
        second++;
        res = strtol(second, &end, 10);
        if (errno)
            pfatal("strtol");
        if (res > USHRT_MAX || end == second || *end != '\0')
            fatal("invalid end port: %s", second);
        *to = res;
        if (*to < *from)
            fatal("end port is > start port: %s", str);
    } else {
        *to = *from;
    }
}

long cli_count_ports(int argc, char **argv)
{
    int i;
    long start, end, cnt;
    for (i = 0, cnt = 0; i < argc; i++) {
        cli_parse_range(argv[i], &start, &end);
        cnt += end-start+1;
    }
    return cnt;
}

void cli_open_sockets(int argc, char **argv,
                      void (*call)(int,void*), void *arg)
{
    int i, fd;
    long start, end, port;
    for (i = 0; i < argc; i++) {
        cli_parse_range(argv[i], &start, &end);
        for (port = start; port <= end; port++) {
            fd = create_listen_socket(port);
            if (call)
                call(fd, arg);
        }
    }
}

int fdlist_add(struct bcast_data *bc_data, struct epoll_fd_data *fd_data)
{
    size_t i, newmax;
    void *newfds;

    if (!fd_data) {
        errno = EINVAL;
        return -1;
    }

    /* try to reuse existing free place */
    for (i = 0; i < bc_data->used_max; i++) {
        if (!bc_data->fds[i]) {
            bc_data->fds[i] = fd_data;
            bc_data->used++;
            return 0;
        }
    }

    /* try to use existing allocated space */
    if (bc_data->used_max < bc_data->max) {
        bc_data->fds[bc_data->used_max] = fd_data;
        bc_data->used_max++;
        bc_data->used++;
        return 0;
    }

    /* allocate new space */
    newmax = bc_data->fds ? bc_data->max*2 : 2;
    newfds = realloc(bc_data->fds, newmax*sizeof(struct fd_data *));
    if (!newfds)
        return -1;
    bc_data->fds = newfds;
    bc_data->max = newmax;
    bc_data->fds[bc_data->used_max] = fd_data;
    bc_data->used_max++;
    bc_data->used++;

    return 0;
}
int fdlist_del(struct bcast_data *bc_data, struct epoll_fd_data *fd_data)
{
    size_t i;

    if (!bc_data->fds || !fd_data)
        return -1;

    for (i = 0; i < bc_data->used_max; i++) {
        if (bc_data->fds[i] == fd_data) {
            bc_data->fds[i] = NULL;
            bc_data->used--;
            if (bc_data->used == 0) {
                bc_data->max = bc_data->used_max = 0;
                free(bc_data->fds);
                bc_data->fds = NULL;
            }
            break;
        }
    }

    return 0;
}

void epoll_add_listen(int listenfd, void *arg)
{
    int epollfd = *(int*)arg;

    struct epoll_event event;
    struct epoll_fd_data *fd_data;
    struct bcast_data *bc_data;

    fd_data = malloc(sizeof(struct epoll_fd_data));
    if (!fd_data)
        pfatal("malloc(struct epoll_fd_data)");
    bc_data = malloc(sizeof(struct bcast_data));
    if (!bc_data)
        pfatal("malloc(struct bcast_data)");

    bc_data->used = 0;
    bc_data->used_max = 0;
    bc_data->max = 0;
    bc_data->fds = NULL;

    fd_data->listening = true;
    fd_data->fd = listenfd;
    fd_data->data = bc_data;

    event.events = EPOLLIN | EPOLLPRI;
    event.data.ptr = fd_data;

    if (epoll_ctl(epollfd, EPOLL_CTL_ADD, listenfd, &event) == -1)
        pfatal("epoll_ctl(EPOLL_CTL_ADD)");
}

void epoll_close_fd(int epollfd, struct epoll_fd_data *fd_data)
{
    int fd;
    struct bcast_data *bc_data;
    (void) epollfd;

    fd = fd_data->fd;
    bc_data = fd_data->data;

    /* fd may be already invalid, do just close() as we're not dup()ing it */
    //if (epoll_ctl(epollfd, EPOLL_CTL_DEL, fd, NULL) == -1)
    //    pfatal("epoll_ctl(EPOLL_CTL_DEL)");
    close(fd);

    if (bc_data) {
        if (fd_data->listening) {
            /* free all shared structures, not just for the one fd */
            if (bc_data->fds)
                free(bc_data->fds);
            free(bc_data);
        } else {
            fdlist_del(bc_data, fd_data);
        }
    }
    free(fd_data);
}

void handle_connect(int epollfd, struct epoll_fd_data *fd_data_l)
{
    int client;
    struct epoll_event event;
    struct bcast_data *bc_data = fd_data_l->data;
    struct epoll_fd_data *fd_data_c = NULL;

    client = accept(fd_data_l->fd, NULL, NULL);
    if (client == -1) {
        /*
         * this is actually a much bigger shitstorm than it seems
         *
         * When accept() starts failing with ENFILE/EMFILE, it keeps the
         * original un-accepted connections in listen backlog, in the kernel.
         * This is a problem both when using EPOLLET (event gets triggered once,
         * but never again, even if we have free descriptors, "freezing" the
         * listen fd) and when not (event keeps re-triggering in an endless
         * CPU-eating loop, never processing any other event).
         *
         * While we could use EPOLLET and just accept() when we get free
         * descriptors, epoll_wait won't wake us and we cannot access its
         * kernel metadata from our main(), ie. with epoll_wait timeout.
         * We also cannot reject the connections because we don't have fds
         * for them when the ENFILE/EMFILE happens. We cannot keep a spare fd
         * to free() just so we can accept() because ENFILE is system-wide and
         * another process might win the race.
         *
         * Essentially, we can
         * 1) use EPOLLET and leave the listen fd frozen, waiting forever
         *    for unaccepted kernel-side CLOSE_WAIT conns in the listen backlog
         *    (or close the listen fd straight away)
         * 2) allocate extra memory and run extra code to keep track of listen
         *    fds with unaccepted connections in backlog
         * 3) just give up and exit
         *
         * since this is a very rare corner case the admin should resolve ASAP,
         * I went with 3
         */
        pfatal("accept");
    }

    if (set_nonblocking(client) == -1) {
        perror("set_nonblocking");
        goto client_err;
    }

    fd_data_c = malloc(sizeof(struct epoll_fd_data));
    if (!fd_data_c) {
        perror("malloc(struct epoll_fd_data)");
        goto client_err;
    }

    fd_data_c->listening = false;
    fd_data_c->fd = client;
    fd_data_c->data = bc_data;

    if (fdlist_add(bc_data, fd_data_c) == -1) {
        perror("fdlist_add");
        goto client_err;
    }

    event.events = EPOLLIN | EPOLLPRI;
    event.data.ptr = fd_data_c;

    if (epoll_ctl(epollfd, EPOLL_CTL_ADD, client, &event) == -1) {
        perror("epoll_ctl(EPOLL_CTL_ADD)");
        goto client_err;
    }

    return;

client_err:
    fdlist_del(bc_data, fd_data_c);
    free(fd_data_c);
}

void handle_recvsend(int epollfd, struct epoll_fd_data *fd_data)
{
    ssize_t recvd;
    size_t i;
    struct bcast_data *bc_data = fd_data->data;
    char buff[1024];

    recvd = recv(fd_data->fd, buff, sizeof(buff), 0);
    if (recvd < 1) {
        epoll_close_fd(epollfd, fd_data);
        return;
    }

    for (i = 0; i < bc_data->used_max; i++) {
        if (!bc_data->fds[i] || bc_data->fds[i] == fd_data)
            continue;
        if (send(bc_data->fds[i]->fd, buff, recvd, MSG_NOSIGNAL) == -1)
            if (errno != EAGAIN && errno != EWOULDBLOCK)
                epoll_close_fd(epollfd, bc_data->fds[i]);
    }
}

int main(int argc, char **argv)
{
    unsigned long portcount;
    int epollfd, ready, i;
    struct epoll_event events[64];
    uint32_t evmask;
    struct epoll_fd_data *fd_data;

    if (argc < 2)
        fatal("Usage: tcp_repeater2 <fromport[-toport]> [fromport-[toport]] ...");

    portcount = cli_count_ports(argc-1, argv+1);
    raise_nofile_rlimit(portcount + MIN_CLIENTS);

    epollfd = epoll_create(portcount);
    if (epollfd == -1)
        pfatal("epoll_create");

    cli_open_sockets(argc-1, argv+1, epoll_add_listen, &epollfd);

    drop_privileges(UNPRIVILEGED_USER, UNPRIVILEGED_GROUP);

    while (1) {
        ready = epoll_wait(epollfd, events,
                           sizeof(events)/sizeof(struct epoll_event), -1);
        if (ready == -1)
            pfatal("epoll_wait");

        for (i = 0; i < ready; i++) {
            evmask = events[i].events;
            fd_data = events[i].data.ptr;

            if (evmask & (EPOLLERR | EPOLLHUP)) {
                epoll_close_fd(epollfd, fd_data);
            } else if (evmask & (EPOLLIN | EPOLLPRI)) {
                if (fd_data->listening)
                    handle_connect(epollfd, fd_data);
                else
                    handle_recvsend(epollfd, fd_data);
            } else {
                err("got unknown epoll flags %"PRIu32" on fd %d, closing it",
                    evmask, fd_data->fd);
                epoll_close_fd(epollfd, fd_data);
            }
        }
    }

    return EXIT_SUCCESS;
}
