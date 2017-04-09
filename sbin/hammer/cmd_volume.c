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
 * Volume operations:
 *
 *   - volume-add: Add new volume to HAMMER filesystem
 *   - volume-del: Remove volume from HAMMER filesystem
 *   - volume-list: List volumes making up a HAMMER filesystem
 *   - volume-blkdevs: List volumes making up a HAMMER filesystem
 *     in blkdevs format
 */

#include "hammer.h"

/*
 * volume-add <device> <filesystem>
 */
void
hammer_cmd_volume_add(char **av, int ac)
{
	struct hammer_ioc_volume ioc;
	struct volume_info *volume;
	int fd;
	const char *device, *filesystem;

	if (ac != 2)
		errx(1, "hammer volume-add <device> <filesystem>");

	device = av[0];
	filesystem = av[1];

        fd = open(filesystem, O_RDONLY);
	if (fd < 0)
		err(1, "hammer volume-add: unable to access %s", filesystem);

	/*
	 * Initialize and check the device
	 */
	volume = init_volume(device, O_RDONLY, -1);
	assert(volume->vol_no == -1);
	if (strcmp(volume->type, "DEVICE"))
		errx(1, "Not a block device: %s", device);
	close(volume->fd);

	/*
	 * volume-add ioctl
	 */
	bzero(&ioc, sizeof(ioc));
	strncpy(ioc.device_name, device, MAXPATHLEN);
	ioc.vol_size = volume->size;
	ioc.boot_area_size = init_boot_area_size(0, ioc.vol_size);
	ioc.memory_log_size = init_memory_log_size(0, ioc.vol_size);

	if (ioctl(fd, HAMMERIOC_ADD_VOLUME, &ioc) < 0)
		err(1, "hammer volume-add ioctl");

	close(fd);
	hammer_cmd_volume_list(av + 1, ac - 1);
}

/*
 * volume-del <device> <filesystem>
 */
void
hammer_cmd_volume_del(char **av, int ac)
{
	struct hammer_ioc_volume ioc;
	int fd, retried = 0;
	const char *device, *filesystem;

	if (ac != 2)
		errx(1, "hammer volume-del <device> <filesystem>");

	device = av[0];
	filesystem = av[1];

        fd = open(filesystem, O_RDONLY);
	if (fd < 0)
		err(1, "hammer volume-del: unable to access %s", filesystem);

	/*
	 * volume-del ioctl
	 */
	bzero(&ioc, sizeof(ioc));
	strncpy(ioc.device_name, device, MAXPATHLEN);
	if (ForceOpt)
		ioc.flag |= HAMMER_IOC_VOLUME_REBLOCK;
retry:
	if (ioctl(fd, HAMMERIOC_DEL_VOLUME, &ioc) < 0) {
		if ((errno == ENOTEMPTY) && (retried++ == 0)) {
			printf("%s is not empty, ", device);
			printf("do you want to reblock %s? [y/n] ", device);
			fflush(stdout);
			if (getyn() == 1) {
				ioc.flag |= HAMMER_IOC_VOLUME_REBLOCK;
				goto retry;
			}
		}
		err(1, "hammer volume-del ioctl");
	}

	close(fd);
	hammer_cmd_volume_list(av + 1, ac - 1);
}

/*
 * volume-list <filesystem>
 */
void
hammer_cmd_volume_list(char **av, int ac)
{
	struct hammer_ioc_volume_list ioc;
	char *device_name;
	int vol_no, i;

	if (ac < 1)
		errx(1, "hammer volume-list <filesystem>");

	if (hammer_fs_to_vol(av[0], &ioc) == -1)
		errx(1, "hammer volume-list: failed");

	for (i = 0; i < ioc.nvols; i++) {
		device_name = ioc.vols[i].device_name;
		vol_no = ioc.vols[i].vol_no;
		if (VerboseOpt)
			printf("%d\t%s%s\n", vol_no, device_name,
				(vol_no == HAMMER_ROOT_VOLNO ?
				" (Root Volume)" : ""));
		else
			printf("%s\n", device_name);
	}

	free(ioc.vols);
}

/*
 * volume-blkdevs <filesystem>
 */
void
hammer_cmd_volume_blkdevs(char **av, int ac)
{
	struct hammer_ioc_volume_list ioc;
	int i;

	if (ac < 1)
		errx(1, "hammer volume-blkdevs <filesystem>");

	if (hammer_fs_to_vol(av[0], &ioc) == -1)
		errx(1, "hammer volume-list: failed");

	for (i = 0; i < ioc.nvols; i++) {
		printf("%s", ioc.vols[i].device_name);
		if (i != ioc.nvols - 1)
			printf(":");
	}
	printf("\n");

	free(ioc.vols);
}
