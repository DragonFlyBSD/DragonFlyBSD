/*
 * Copyright (c) 2007 The DragonFly Project.  All rights reserved.
 * 
 * This code is derived from software contributed to The DragonFly Project
 * by Matthew Dillon <dillon@backplane.com>
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
 */

#define DKTYPENAMES
#include <sys/param.h>
#include <sys/fcntl.h>
#include <sys/dtype.h>
#include <sys/diskslice.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <uuid.h>

static int quietMode;
static int extendedMode;

static void dumppart(const char *path, struct partinfo *dpart);
static void usage(const char *av0);

int
main(int ac, char **av)
{
	struct partinfo dpart;
	int i;
	int ch;
	int fd;

	while ((ch = getopt(ac, av, "qx")) != -1) {
		switch(ch) {
		case 'q':
			quietMode = 1;
			break;
		case 'x':
			extendedMode = 1;
			break;
		default:
			usage(av[0]);
		}
	}
	for (i = optind; i < ac; ++i) {
		if ((fd = open(av[i], O_RDONLY)) < 0) {
			if (errno != ENXIO || quietMode == 0)
				printf("%-16s %s\n", av[i], strerror(errno));
			continue;
		}
		bzero(&dpart, sizeof(dpart));
		if (ioctl(fd, DIOCGPART, &dpart) < 0) {
			printf("%-16s %s\n", av[i], strerror(errno));
		} else if (dpart.media_size == 0 && dpart.fstype == 0 &&
			   uuid_is_nil(&dpart.fstype_uuid, NULL)) {
			if (quietMode == 0)
				printf("%-16s unused\n", av[i]);
		} else {
			dumppart(av[i], &dpart);
		}
		close(fd);
	}
	return(0);
}

static
void
dumppart(const char *path, struct partinfo *dpart)
{
	printf("%-16s ", path);
	printf("blksize=%-4d offset=0x%012jx size=0x%012jx ",
		dpart->media_blksize,
		(uintmax_t)dpart->media_offset,
		(uintmax_t)dpart->media_size
	);
	if (dpart->media_size >= 100LL*1024*1024*1024) {
		printf("%7.2f GB", 
			(double)dpart->media_size / 
			(1024.0*1024.0*1024.0)
		);
	} else if (dpart->media_size >= 1024*1024) {
		printf("%7.2f MB", 
			(double)dpart->media_size / 
			(1024.0*1024.0)
		);
	} else {
		printf("%7.2f KB", 
			(double)dpart->media_size / 1024.0
		);
	}
	if (extendedMode) {
		if (!uuid_is_nil(&dpart->fstype_uuid, NULL)) {
			char *str;
			uuid_addr_lookup(&dpart->fstype_uuid, &str, NULL);
			if (str == NULL)
				uuid_to_string(&dpart->fstype_uuid, &str, NULL);
			printf(" fstype='%s'", str ? str : "<illegal>");
		}
		if (dpart->fstype) {
			printf(" ofstype=");
			if (dpart->fstype < 0 || 
			    dpart->fstype >= (int)FSMAXTYPES) {
				printf("%d", dpart->fstype);
			} else {
				printf("%s", fstypenames[dpart->fstype]);
			}
		}
		if (!uuid_is_nil(&dpart->storage_uuid, NULL)) {
			char *str = NULL;
			uuid_to_string(&dpart->storage_uuid, &str, NULL);
			printf(" storage_uuid='%s'", str ? str : "<illegal>");
		}
		if (dpart->reserved_blocks) {
			/*
			 * note: rsvdlabel is inclusive of rsvdplat. i.e.
			 * they are not relative to each other.
			 */
			printf(" reserved=%jd",
				(intmax_t)dpart->reserved_blocks);
		}
	}
	printf("\n");
}

static
void
usage(const char *av0)
{
	fprintf(stderr, "%s [-qx] device ...\n", av0);
	exit(1);
}

