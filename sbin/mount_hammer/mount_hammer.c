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
 * $DragonFly: src/sbin/mount_hammer/mount_hammer.c,v 1.3 2007/12/12 23:47:57 dillon Exp $
 */

#include <sys/types.h>
#include <sys/diskslice.h>
#include <sys/diskmbr.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <vfs/hammer/hammer_mount.h>

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

#include "mntopts.h"

static void hammer_parsetime(u_int64_t *tidp, const char *timestr);

#define MOPT_HAMMEROPTS		\
	{ "history", 1, HMNT_NOHISTORY, 1 }

static struct mntopt mopts[] = { MOPT_STDOPTS, MOPT_HAMMEROPTS, MOPT_NULL };

static void usage(void);

int
main(int ac, char **av)
{
	struct hammer_mount_info info;
	struct vfsconf vfc;
	int mount_flags;
	int error;
	int ch;
	char *mountpt;

	bzero(&info, sizeof(info));
	info.asof = 0;
	mount_flags = 0;
	info.hflags = 0;

	while ((ch = getopt(ac, av, "o:T:")) != -1) {
		switch(ch) {
		case 'T':
			hammer_parsetime(&info.asof, optarg);
			break;
		case 'o':
			getmntopts(optarg, mopts, &mount_flags, &info.hflags);
			break;
		default:
			usage();
			/* not reached */
		}
	}
	ac -= optind;
	av += optind;

	if (ac < 2) {
		usage();
		/* not reached */
	}

	/*
	 * Mount arguments: vol [vol...] mountpt
	 */
	info.volumes = (const char **)av;
	info.nvolumes = ac - 1;
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

	if (mount(vfc.vfc_name, mountpt, mount_flags, &info))
		err(1, NULL);
	exit(0);
}

/*
 * Parse a timestamp for the mount point
 *
 * yyyymmddhhmmss
 * -N[s/h/d/m/y]
 */
static
void
hammer_parsetime(u_int64_t *tidp, const char *timestr)
{
	struct tm tm;
	time_t t;
	int32_t n;
	char c;
	double seconds = 0;

	t = time(NULL);

	if (*timestr == 0)
		usage();

	if (isalpha(timestr[strlen(timestr)-1])) {
		if (sscanf(timestr, "%d%c", &n, &c) != 2)
			usage();
		switch(c) {
		case 'Y':
			n *= 365;
			goto days;
		case 'M':
			n *= 30;
			/* fall through */
		case 'D':
		days:
			n *= 24;
			/* fall through */
		case 'h':
			n *= 60;
			/* fall through */
		case 'm':
			n *= 60;
			/* fall through */
		case 's':
			t -= n;
			break;
		default:
			usage();
		}
	} else {
		localtime_r(&t, &tm);
		seconds = (double)tm.tm_sec;
		tm.tm_year -= 1900;
		tm.tm_mon -= 1;
		n = sscanf(timestr, "%4d%2d%2d:%2d%2d%lf",
			   &tm.tm_year, &tm.tm_mon, &tm.tm_mday,
			   &tm.tm_hour, &tm.tm_min, &seconds);
		tm.tm_mon += 1;
		tm.tm_year += 1900;
		tm.tm_sec = (int)seconds;
		t = mktime(&tm);
	}
	localtime_r(&t, &tm);
	printf("mount_hammer as-of %s", asctime(&tm));
	*tidp = (u_int64_t)t * 1000000000 + 
		(seconds - (int)seconds) * 1000000000;
}

static
void
usage(void)
{
	fprintf(stderr, "mount_hammer [-T time] [-o options] "
			"volume [volume...] mount_pt");
	fprintf(stderr, "    time: +n[s/m/h/D/M/Y]\n"
			"    time: yyyymmdd[:hhmmss]\n");
	exit(1);
}
