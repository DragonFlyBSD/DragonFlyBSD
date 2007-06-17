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
 * 
 * $DragonFly: src/sbin/diskinfo/diskinfo.c,v 1.4 2007/06/17 23:50:15 dillon Exp $
 */

#define DKTYPENAMES
#include <sys/types.h>
#include <sys/fcntl.h>
#include <sys/dtype.h>
#include <sys/diskslice.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>

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
			   dpart.fstypestr[0] == 0) {
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
	printf("blksize=%-4d off=%012llx size=%012llx ",
		dpart->media_blksize,
		dpart->media_offset,
		dpart->media_size
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
		if (dpart->fstypestr[0]) {
			printf(" fs=%s", dpart->fstypestr);
		} else if (dpart->fstype) {
			printf(" fs=");
			if (dpart->fstype < 0 || 
			    dpart->fstype >= (int)FSMAXTYPES) {
				printf("%d", dpart->fstype);
			} else {
				printf("%s", fstypenames[dpart->fstype]);
			}
		}
		if (dpart->reserved_blocks) {
			/*
			 * note: rsvdlabel is inclusive of rsvdplat. i.e.
			 * they are not relative to each other.
			 */
			printf(" reserved=%lld", dpart->reserved_blocks);
		}
	}
	printf("\n");
}

static
void
usage(const char *av0)
{
	fprintf(stderr, "%s [-q] [devices....]\n", av0);
	exit(1);
}

