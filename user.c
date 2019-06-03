/*
 * Copyright (c) 2014 Red Hat, Inc. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA
 * 02110-1301, USA.
 */

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
