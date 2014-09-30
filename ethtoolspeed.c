#define _GNU_SOURCE

#include <netinet/in.h>
#include <linux/sockios.h>
#include <linux/if.h>
#include <linux/ethtool.h>
#include <stdlib.h>
#include <dlfcn.h>

/* define faked speed here */
#define SPEED 12345

static int (*real_ioctl)(int fd, int request, void *data) = NULL;

int ioctl(int fd, int request, void *data)
{
    int rc;
    struct ethtool_cmd *edata;

    if (real_ioctl == NULL)
        real_ioctl = dlsym(RTLD_NEXT, "ioctl");

    if (request == SIOCETHTOOL) {
        rc = real_ioctl(fd, request, data);

        edata = ((struct ifreq *)data)->ifr_data;
        edata->speed = SPEED;

        return rc;
    }

    return real_ioctl(fd, request, data);
}
