#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>

void dummy(void)
{
	return;
}

int main(int argc, char **argv)
{
	int fd;

	if (argc < 2) {
		fprintf(stderr, "usage: %s <path>\n", argv[0]);
		return 1;
	}

	fd = open(argv[1], O_WRONLY);
	if (!fd) {
		perror("open");
		return 1;
	}

	/* simply provide an userspace address for the kernel
	 * to read (smap) or execute (smep) */
	if (!write(fd, &dummy, sizeof(void*))) {
		perror("write");
		return 1;
	}

	close(fd);

	return 0;
}
