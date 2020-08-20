/*
 * Copyright (c) 2020 Red Hat, Inc. All rights reserved.
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
 * this variant doesn't add any actual data, it just increments the entropy
 * estimator's idea of entropy, resulting in a probably insecure system
 *
 * to build:
 *   cc -Wall -Wextra -o fake-seeder fake-seeder.c
 */

#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <linux/random.h>

/* how much bits to credit in one cycle */
#define ENTROPY_COUNT 1000

/* comment out to run forever */
#define STOP_WHEN_ABOVE 4000

int main()
{
    int adjust = ENTROPY_COUNT;
    int random_fd;
    int ent_cnt;

    random_fd = open("/dev/random", O_WRONLY);
    if (random_fd == -1) {
        perror("open(/dev/random)");
        goto err;
    }

    while (1) {
        if (ioctl(random_fd, RNDADDTOENTCNT, &adjust) < 0) {
            perror("ioctl(RNDADDTOENTCNT)");
            goto err;
        }
        if (ioctl(random_fd, RNDGETENTCNT, &ent_cnt) < 0) {
            perror("ioctl(RNDGETENTCNT)");
            goto err;
        }
        fprintf(stderr, "credited %d bits, kernel now has %d bits\n",
                adjust, ent_cnt);
        fflush(stderr);

#ifdef STOP_WHEN_ABOVE
        if (ent_cnt > STOP_WHEN_ABOVE)
            break;
#endif

        usleep(10*1000);
    }

    close(random_fd);
    return 0;

err:
    if (random_fd)
        close(random_fd);
    return 1;
}
