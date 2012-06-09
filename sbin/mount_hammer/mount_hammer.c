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

#include <sys/types.h>
#include <sys/diskslice.h>
#include <sys/diskmbr.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/syslimits.h>
#include <vfs/hammer/hammer_mount.h>
#include <vfs/hammer/hammer_disk.h>

#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <stddef.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>
#include <fcntl.h>
#include <uuid.h>
#include <err.h>
#include <assert.h>
#include <ctype.h>
#include <mntopts.h>

typedef const char **ary_ptr_t;

static void extract_volumes(ary_ptr_t *aryp, int *countp, char **av, int ac);

#define MOPT_UPDATE         { "update",     0, MNT_UPDATE, 0 }

#define MOPT_HAMMEROPTS		\
	{ "history", 1, HMNT_NOHISTORY, 1 },	\
	{ "master=", 0, HMNT_MASTERID, 1 },	\
	{ "mirror", 1, HMNT_MASTERID, 1 }

static struct mntopt mopts[] = { MOPT_STDOPTS, MOPT_HAMMEROPTS,
				 MOPT_UPDATE, MOPT_NULL };

static void usage(void);

int
main(int ac, char **av)
{
	struct hammer_mount_info info;
	struct vfsconf vfc;
	struct hammer_volume_ondisk *od;
	int mount_flags = 0;
	int error;
	int ch;
	int init_flags = 0;
	int ax;
	int fd;
	int pr;
	int fdevs_size;
	char *mountpt;
	char *ptr;
	char *fdevs;


	bzero(&info, sizeof(info));
	info.asof = 0;
	mount_flags = 0;
	info.hflags = 0;

	while ((ch = getopt(ac, av, "o:T:u")) != -1) {
		switch(ch) {
		case 'T':
			info.asof = strtoull(optarg, NULL, 0);
			break;
		case 'o':
			getmntopts(optarg, mopts, &mount_flags, &info.hflags);


			/*
			 * Handle extended flags with parameters.
			 */
			if (info.hflags & HMNT_MASTERID) {
				ptr = strstr(optarg, "master=");
				if (ptr) {
					info.master_id = strtol(ptr + 7, NULL, 0);
					if (info.master_id == 0) {
						fprintf(stderr,
	"hammer_mount: Warning: a master id of 0 is the default, explicit\n"
	"settings should probably use 1-15\n");
					}
				}
				ptr = strstr(optarg, "nomirror");
				if (ptr)
					info.master_id = -1;
			}
			break;
		case 'u':
			init_flags |= MNT_UPDATE;
			break;
		default:
			usage();
			/* not reached */
		}
	}
	ac -= optind;
	av += optind;
	mount_flags |= init_flags;

	/*
	 * Only the mount point need be specified in update mode.
	 */
	if (init_flags & MNT_UPDATE) {
		if (ac != 1) {
			usage();
			/* not reached */
		}
		mountpt = av[0];
		if (mount(vfc.vfc_name, mountpt, mount_flags, &info))
			err(1, "mountpoint %s", mountpt);
		exit(0);
	}

	if (ac < 2) {
		usage();
		/* not reached */
	}

	/*
	 * Mount arguments: vol [vol...] mountpt
	 */
	extract_volumes(&info.volumes, &info.nvolumes, av, ac - 1);
	mountpt = av[ac - 1];

	/*
	 * Load the hammer module if necessary (this bit stolen from
	 * mount_null).
	 */
	error = getvfsbyname("hammer", &vfc);
	if (error && vfsisloadable("hammer")) {
		if (vfsload("hammer") != 0)
			err(1, "vfsload(hammer)");
		endvfsent();
		error = getvfsbyname("hammer", &vfc);
	}
	if (error)
		errx(1, "hammer filesystem is not available");

	if (mount(vfc.vfc_name, mountpt, mount_flags, &info)) {
		/* Build fdevs in case of error to report failed devices */
		fdevs_size = ac * PATH_MAX;
		fdevs = malloc(fdevs_size);
		for (ax = 0; ax < ac - 1; ax++) {
			fd = open(info.volumes[ax], O_RDONLY);
			if (fd < 0 ) {
				printf ("%s: open failed\n", info.volumes[ax]);
				strlcat(fdevs, info.volumes[ax], fdevs_size);
				if (ax < ac - 2)
					strlcat(fdevs, " ", fdevs_size);
				continue;
			}

			od = malloc(HAMMER_BUFSIZE);
			if (od == NULL) {
				close (fd);
				perror("malloc");
				continue;
			}

			bzero(od, HAMMER_BUFSIZE);
			pr = pread(fd, od, HAMMER_BUFSIZE, 0);
			if (pr != HAMMER_BUFSIZE ||
				od->vol_signature != HAMMER_FSBUF_VOLUME) {
					printf("%s: Not a valid HAMMER filesystem\n", info.volumes[ax]);
					strlcat(fdevs, info.volumes[ax], fdevs_size);
					if (ax < ac - 2)
						strlcat(fdevs, " ", fdevs_size);
			}
			close(fd);
		}
		err(1,"mount %s on %s", fdevs, mountpt);
	}
	exit (0);
}

/*
 * Extract a volume list
 */
static void
extract_volumes(ary_ptr_t *aryp, int *countp, char **av, int ac)
{
	int idx = 0;
	int arymax = 32;
	const char **ary = malloc(sizeof(char *) * 32);
	char *ptr;
	char *next;

	while (ac) {
		if (idx == arymax) {
			arymax += 32;
			ary = realloc(ary, sizeof(char *) * arymax);
		}
		if (strchr(*av, ':') == NULL) {
			ary[idx++] = *av;
		} else {
			next = strdup(*av);
			while ((ptr = next) != NULL) {
				if (idx == arymax) {
					arymax += 32;
					ary = realloc(ary, sizeof(char *) *
						      arymax);
				}
				if ((next = strchr(ptr, ':')) != NULL)
					*next++ = 0;
				ary[idx++] = ptr;
			}
		}
		--ac;
		++av;
		
	}
	*aryp = ary;
	*countp = idx;
}

static
void
usage(void)
{
	fprintf(stderr, "usage: mount_hammer [-o options] [-T transaction-id] "
			"special ... node\n");
	fprintf(stderr, "       mount_hammer [-o options] [-T transaction-id] "
			"special[:special]* node\n");
	fprintf(stderr, "       mount_hammer -u [-o options] node\n");
	exit(1);
}
