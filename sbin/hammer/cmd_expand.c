/*
 * Copyright (c) 2009 The DragonFly Project.  All rights reserved.
 *
 * This code is derived from software contributed to The DragonFly Project
 * by Matthew Dillon <dillon@backplane.com> and
 * Michael Neumann <mneumann@ntecs.de>
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in
 *    the documentation and/or other materials provided with the
 *    distribution.
 * 3. Neither the name of The DragonFly Project nor the names of its
 *    contributors may be used to endorse or promote products derived
 *    from this software without specific, prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS
 * FOR A PARTICULAR PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE
 * COPYRIGHT HOLDERS OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY OR CONSEQUENTIAL DAMAGES (INCLUDING,
 * BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED
 * AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
 * OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT
 * OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 */
/*
 * Expand a HAMMER filesystem.
 */

#include "hammer.h"
#include <string.h>

static uint64_t check_volume(const char *vol_name);
static void expand_usage(int exit_code);

/*
 * expand <filesystem> <device>
 */
void
hammer_cmd_expand(char **av, int ac)
{
	struct hammer_ioc_expand expand;
	struct statfs sfs;
	int fd;

	if (ac != 2)
		expand_usage(1);
        fd = open(av[0], O_RDONLY);
	if (fd < 0) {
		fprintf(stderr, "hammer expand: unable to access %s: %s\n",
			av[0], strerror(errno));
		exit(1);
	}

	/*
	 * Make sure we aren't trying to expand the root filesystem.  The
	 * kernel can't handle multi-volume root mounts.
	 */
	if (fstatfs(fd, &sfs) < 0) {
		fprintf(stderr, "hammer expand: statvfs failed on %s: %s\n",
			av[0], strerror(errno));
		exit(1);
	}
	if (strcmp(sfs.f_mntonname, "/") == 0 || sfs.f_mntonname[0] == 0) {
		fprintf(stderr,
			"hammer expand: Refused attempt to expand root fs.\n"
			"The kernel is unable to boot from multi-volume\n"
			"HAMMER root filesystems.\n");
		exit(1);
	}

	/*
	 * Expansion ioctl
	 */
	bzero(&expand, sizeof(expand));
	strncpy(expand.device_name, av[1], MAXPATHLEN);
	expand.vol_size = check_volume(av[1]);
	expand.boot_area_size = HAMMER_BOOT_NOMBYTES;
	expand.mem_area_size = HAMMER_MEM_NOMBYTES;

	if (ioctl(fd, HAMMERIOC_EXPAND, &expand) < 0) {
		fprintf(stderr, "hammer expand ioctl: %s\n", strerror(errno));
		exit(1);
	}

	close(fd);
}

static
void
expand_usage(int exit_code)
{
	fprintf(stderr, "hammer expand <filesystem> <device>\n");
	exit(exit_code);
}

/*
 * Check basic volume characteristics.  HAMMER filesystems use a minimum
 * of a 16KB filesystem buffer size.
 *
 * Returns the size of the device.
 *
 * From newfs_hammer.c
 */
static
uint64_t
check_volume(const char *vol_name)
{
	struct partinfo pinfo;
	int fd;

	/*
	 * Get basic information about the volume
	 */
	fd = open(vol_name, O_RDWR);
	if (fd < 0)
		errx(1, "Unable to open %s R+W", vol_name);

	if (ioctl(fd, DIOCGPART, &pinfo) < 0) {
		errx(1, "No block device: %s", vol_name);
	}
	/*
	 * When formatting a block device as a HAMMER volume the
	 * sector size must be compatible. HAMMER uses 16384 byte
	 * filesystem buffers.
	 */
	if (pinfo.reserved_blocks) {
		errx(1, "HAMMER cannot be placed in a partition "
			"which overlaps the disklabel or MBR");
	}
	if (pinfo.media_blksize > 16384 ||
	    16384 % pinfo.media_blksize) {
		errx(1, "A media sector size of %d is not supported",
		     pinfo.media_blksize);
	}

	close(fd);
	return pinfo.media_size;
}
