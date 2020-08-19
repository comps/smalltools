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
 * this variant calls Intel's RDRAND instruction repeatedly to fill
 * a buffer and then passes it to kernel via RNDADDENTROPY, crediting
 * all data as valid entropy
 *
 * to build:
 *   gcc -Wall -Wextra -mrdrnd -o rdrand-seeder rdrand-seeder.c
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <linux/random.h>
#include <immintrin.h>

/* in bytes, must be divisible both by 4 (u32) */
#define RNG_BUFSIZE 128

/* uncomment to stop when the kernel entropy pool gets reasonably full */
//#define STOP_WHEN_ABOVE 4000

/* uncomment to write raw bytes to stdout, for statistical analysis */
//#define DATA_TO_STDOUT

int main()
{
    ssize_t ret;
    size_t cnt;
    struct rand_pool_info *rand_pool = NULL;
    int random_fd = 0;
    int ent_cnt = 0;

    rand_pool = malloc(sizeof(struct rand_pool_info) + RNG_BUFSIZE);
    if (!rand_pool)
        goto err;

    random_fd = open("/dev/random", O_WRONLY);
    if (random_fd == -1) {
        perror("open(/dev/random)");
        goto err;
    }

    /*
     * about _rdrand*_step():
     *
     * These intrinsics generate random numbers of 16/32/64 bit wide random
     * integers. The generated random value is written to the given memory
     * location and the success status is returned: '1' if the hardware
     * returned a valid random value, and '0' otherwise.
     *
     * https://software.intel.com/content/www/us/en/develop/documentation/cpp-compiler-developer-guide-and-reference/top/compiler-reference/intrinsics/intrinsics-for-later-generation-intel-core-processor-instruction-extensions/intrinsics-that-generate-random-numbers-of-16-32-64-bit-wide-random-integers/rdrand16-step-rdrand32-step-rdrand64-step.html
     */

    while (1) {
        /* sizeof(u32)/sizeof(uint8_t) == 4 */
        for (cnt = 0; cnt < RNG_BUFSIZE/4; cnt++) {
            ret = _rdrand32_step(&rand_pool->buf[cnt]);
            if (ret != 1) {
                /* try again */
                ret = _rdrand32_step(&rand_pool->buf[cnt]);
                if (ret != 1) {
                    fprintf(stderr, "_rdrand32_step() failed");
                    goto err;
                }
            }
        }

#ifdef DATA_TO_STDOUT
        write(STDOUT_FILENO, rand_pool->buf, RNG_BUFSIZE);
#endif

        /* credit 8 bits of entropy for each 1 byte */
        rand_pool->entropy_count = RNG_BUFSIZE*8;
        /* sizeof(u32)/sizeof(uint8_t) */
        rand_pool->buf_size = RNG_BUFSIZE/4;

        if (ioctl(random_fd, RNDADDENTROPY, rand_pool) < 0) {
            perror("ioctl(RNDADDENTROPY)");
            goto err;
        }
        if (ioctl(random_fd, RNDGETENTCNT, &ent_cnt) < 0) {
            perror("ioctl(RNDGETENTCNT)");
            goto err;
        }
        fprintf(stderr, "added %d bytes, kernel now has %d bits\n",
                RNG_BUFSIZE, ent_cnt);
        fflush(stderr);

#ifdef STOP_WHEN_ABOVE
        if (ent_cnt > STOP_WHEN_ABOVE)
            break;
#endif

        usleep(100*1000);
    }

    close(random_fd);
    return 0;

err:
    if (rand_pool)
        free(rand_pool);
    if (random_fd)
        close(random_fd);
    return 1;
}
