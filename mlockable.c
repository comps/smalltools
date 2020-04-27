/*
 * this simply finds the maximum reasonable amount of memory for allocation
 * available on the system, as indicated by MemAvailable from /proc/meminfo
 * minus any other restrictions like overcommit_ratio
 * returned result is in bytes, rounded to a page boundary
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <sys/mman.h>

size_t mem_avail(void)
{
    FILE *procfp;
    char line[100], infotype[50];
    size_t availkb;

    procfp = fopen("/proc/meminfo", "r");
    if (procfp == NULL) {
        perror("/proc/meminfo");
        exit(1);
    }

    while (fgets(line, sizeof(line), procfp) != NULL) {
        fscanf(procfp, "%49s %zu", infotype, &availkb);
        if (strcmp(infotype, "MemAvailable:") == 0) {
            fclose(procfp);
            return availkb*1024;
        }
    }

    fclose(procfp);
    return 0;
}

size_t min_lockable(size_t max)
{
    long pagesize;
    void *mmapped;

    pagesize = sysconf(_SC_PAGE_SIZE);
    if (pagesize == -1) {
        perror("pagesize");
        exit(1);
    }

    /* round to page boundary */
    max = max - (max % pagesize);

    for (; max != 0; max -= pagesize) {
        mmapped = mmap(NULL, max, PROT_READ | PROT_WRITE,
                       MAP_PRIVATE | MAP_ANONYMOUS | MAP_LOCKED, -1, 0);
        if (mmapped != (void *)-1) {
            munmap(mmapped, max);
            break;
        }
        if (errno != ENOMEM) {
            perror("mmap");
            exit(1);
        }
    }

    return max;
}

int main()
{
    printf("%zu\n", min_lockable(mem_avail()));
    return 0;
}
