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
 * this tool uses libkcapi to read random bytes from the in-kernel
 * jitterentropy_rng and passes and credits them as valid entropy bits
 * via RNDADDENTROPY
 *
 * inspired by libkcapi's apps/kcapi-rng.c
 *
 * to build:
 *   cc -Wall -Wextra -o jitter-seeder jitter-seeder.c -lkcapi
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <linux/random.h>
#include <kcapi.h>

/* from kcapi-rng.c ; divisible by 4 because struct rand_pool_info uses u32 */
#define KCAPI_RNG_BUFSIZE 128

/* comment out to run forever */
#define STOP_WHEN_ABOVE 4000

/* uncomment to write raw bytes to stdout, for statistical analysis */
//#define DATA_TO_STDOUT

int main()
{
    /*
     * while buf could theoretically be shared between kcapi_* and struct
     * rand_pool_info, endianess / alignment / code audibility issues would
     * make this a bad choice here due to different buf (uint8_t vs u32)
     * element sizes
     *
     * from random(4):
     *     struct rand_pool_info {
     *         int    entropy_count;
     *         int    buf_size;
     *         __u32  buf[0];
     *     };
     */

    ssize_t ret;
    uint8_t crypto_buf[KCAPI_RNG_BUFSIZE];  /* for both seed and data */
    struct kcapi_handle *rng = NULL;

    struct rand_pool_info *rand_pool = NULL;
    int random_fd = 0;
    int ent_cnt = 0;

    rand_pool = malloc(sizeof(struct rand_pool_info) + KCAPI_RNG_BUFSIZE);
    if (!rand_pool)
        goto err;

    random_fd = open("/dev/random", O_WRONLY);
    if (random_fd == -1) {
        perror("open(/dev/random)");
        goto err;
    }

    ret = kcapi_rng_init(&rng, "jitterentropy_rng", 0);
    if (ret) {
        fprintf(stderr, "kcapi_rng_init() failed: %zd", ret);
        goto err;
    }

    /* should be 0 as jitterentropy_rng shouldn't accept external seed */
    ret = kcapi_rng_seedsize(rng);
    if (ret) {
        fprintf(stderr, "kcapi_rng_seedsize() returned %zd (!= 0)", ret);
        goto err;
    }

    /*
     * as mentioned in kcapi-rng.c, this calls more code in the kernel than just
     * seeding (which doesn't make sense with jitterentropy_rng), so call it
     * with empty buffer and size of 0
     */
    kcapi_memset_secure(crypto_buf, 0, sizeof(crypto_buf));
    ret = kcapi_rng_seed(rng, crypto_buf, 0);
    if (ret) {
        fprintf(stderr, "kcapi_rng_seed() failed: %zd", ret);
        goto err;
    }

    while (1) {
        ret = kcapi_rng_generate(rng, crypto_buf, sizeof(crypto_buf));
        if (ret < 0) {
            fprintf(stderr, "kcapi_rng_generate() failed");
            goto err;
        }
        fprintf(stderr, "got %zd bytes from jitterentropy_rng\n", ret);
        fflush(stderr);

#ifdef DATA_TO_STDOUT
        write(STDOUT_FILENO, crypto_buf, ret);
#endif

        if (ret > 0) {
            /* credit 8 bits of entropy for each 1 byte */
            rand_pool->entropy_count = ret*8;
            /* sizeof(u32)/sizeof(uint8_t) */
            rand_pool->buf_size = ret/4;
            /* never copy partial u32 words if ret%4 != 0 */
            memcpy(rand_pool->buf, crypto_buf, rand_pool->buf_size);

            if (ioctl(random_fd, RNDADDENTROPY, rand_pool) < 0) {
                perror("ioctl(RNDADDENTROPY)");
                goto err;
            }
            if (ioctl(random_fd, RNDGETENTCNT, &ent_cnt) < 0) {
                perror("ioctl(RNDGETENTCNT)");
                goto err;
            }
            fprintf(stderr, "added %zd bytes, kernel now has %d bits\n",
                    ret, ent_cnt);
            fflush(stderr);
        }

#ifdef STOP_WHEN_ABOVE
        if (ent_cnt > STOP_WHEN_ABOVE)
            break;
#endif

        usleep(10*1000);
    }

    kcapi_rng_destroy(rng);
    close(random_fd);
    return 0;

err:
    if (rand_pool)
        free(rand_pool);
    if (rng)
        kcapi_rng_destroy(rng);
    if (random_fd)
        close(random_fd);
    return 1;
}
