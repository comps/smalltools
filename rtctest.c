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
 *
 * AUTHOR: Jiri Jaburek <jjaburek@redhat.com>
 */

#include <stdio.h>
#include <stdlib.h>
#include <linux/rtc.h>
#include <sys/ioctl.h>
#include <sys/time.h>
#include <fcntl.h>
#include <unistd.h>

#define RTCDEV "/dev/rtc0"

int main(int argc, char *argv[])
{
    int fd, rc;
    unsigned long rate;
    time_t limit;
    unsigned long data;
    struct timeval start, end, diff;

    if (argc < 3) {
        fprintf(stderr,
                "usage: rtctest <rate> <maxdelay>\n"
                "       (lower rate means higher delay, also see rtc(4))\n"
                "\n"
                "example: ./rtctest 500 2050   # 2000 in ideal case\n"
                );
        exit(1);
    }
    rate = strtoul(argv[1], NULL, 10);
    limit = strtol(argv[2], NULL, 10);

    fd = open(RTCDEV, O_RDONLY);
    if (fd == -1) {
        perror("open");
        exit(1);
    }

    rc = ioctl(fd, RTC_IRQP_SET, rate);
    if (rc == -1) {
        perror("ioctl(RTC_IRQP_SET)");
        exit(1);
    }

    rc = ioctl(fd, RTC_PIE_ON, 0);
    if (rc == -1) {
        perror("ioctl(RTC_PIE_ON)");
        exit(1);
    }

    while (1) {
        gettimeofday(&start, NULL);

        rc = read(fd, &data, sizeof(unsigned long));
        if (rc == -1) {
            perror("read");
            exit(1);
        }

        gettimeofday(&end, NULL);
        timersub(&end, &start, &diff);
        if (diff.tv_sec > 0 || diff.tv_usec > limit) {
            printf("diff too big: %ld.%06ld\n", diff.tv_sec, diff.tv_usec);
            fflush(stdout);
        }
    }

    rc = ioctl(fd, RTC_PIE_OFF, 0);
    if (rc == -1) {
        perror("ioctl(RTC_PIE_OFF)");
        exit(1);
    }

    close(fd);

    return 0;
}
