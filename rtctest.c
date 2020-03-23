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

/*
 * loosely based on kernel Documentation/rtc.txt
 */
#include <stdio.h>
#include <stdlib.h>
#include <linux/rtc.h>
#include <sys/ioctl.h>
#include <sys/time.h>
#include <fcntl.h>
#include <unistd.h>

/* anything above 2ms should normally not happen */
#define MAX_ACCEPTABLE_USEC 2000

int main()
{
    int fd, rc;
    char *dev;
    long rate;
    long data;
    struct timeval start, end, diff;

    rate = 1024;
    dev = "/dev/rtc0";

    fd = open(dev, O_RDONLY);
    if (fd == -1) {
        perror("open");
        exit(1);
    }

    ioctl(fd, RTC_IRQP_SET, rate);
    rc = ioctl(fd, RTC_IRQP_READ, &rate);
    if (rc == -1) {
        perror("RTC_IRQP_READ");
        exit(1);
    }

    /* Enable periodic interrupts */
    rc = ioctl(fd, RTC_PIE_ON, 0);
    if (rc == -1) {
        perror("RTC_PIE_ON");
        exit(1);
    }

    /*fprintf(stderr, "\nPeriodic IRQ rate is %ldHz.\n", rate);
    fflush(stderr);*/

    while (1) {
        /* set up timers */
        gettimeofday(&start, NULL);

        /* This blocks */
        rc = read(fd, &data, sizeof(unsigned long));
        if (rc == -1) {
            perror("read");
            exit(1);
        }

        /* calc timers */
        gettimeofday(&end, NULL);
        timersub(&end, &start, &diff);
        if (diff.tv_sec > 0 || diff.tv_usec > MAX_ACCEPTABLE_USEC) {
            printf("diff too big: %d.%06d\n", diff.tv_sec, diff.tv_usec);
            fflush(stdout);
        }
    }

    /* Disable periodic interrupts */
    rc = ioctl(fd, RTC_PIE_OFF, 0);
    if (rc == -1) {
        perror("RTC_PIE_OFF ioctl");
        exit(1);
    }

    close(fd);

    return 0;
}
