/*
 * Copyright (c) 2008 The DragonFly Project.  All rights reserved.
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
 * $DragonFly: src/sbin/hammer/cmd_stats.c,v 1.3 2008/07/14 20:28:07 dillon Exp $
 */

#include "hammer.h"
#include <math.h>
#include <sys/sysctl.h>

static void loaddelay(struct timespec *ts, const char *arg);

void
hammer_cmd_bstats(char **av, int ac)
{
	int mibs[8][16];
	size_t lens[8];
	int64_t stats[8];
	int64_t copy[8];
	size_t size;
	struct timespec delay = { 1, 0 };
	int count;
	int i;
	int r;

	if (ac > 0)
		loaddelay(&delay, av[0]);
	lens[0] = 16;
	lens[1] = 16;
	lens[2] = 16;
	lens[3] = 16;
	lens[4] = 16;
	lens[5] = 16;
	r = 0;

	r |= sysctlnametomib("vfs.hammer.stats_btree_elements",
			     mibs[0], &lens[0]);
	r |= sysctlnametomib("vfs.hammer.stats_btree_iterations",
			     mibs[1], &lens[1]);
	r |= sysctlnametomib("vfs.hammer.stats_btree_lookups",
			     mibs[2], &lens[2]);
	r |= sysctlnametomib("vfs.hammer.stats_btree_inserts",
			     mibs[3], &lens[3]);
	r |= sysctlnametomib("vfs.hammer.stats_btree_deletes",
			     mibs[4], &lens[4]);
	r |= sysctlnametomib("vfs.hammer.stats_btree_splits",
			     mibs[5], &lens[5]);
	if (r < 0) {
		perror("sysctl: HAMMER stats not available:");
		exit(1);
	}

	for (count = 0; ; ++count) {
		for (i = 0; i < 6; ++i) {
			size = sizeof(stats[0]);
			r = sysctl(mibs[i], lens[i], &stats[i], &size, NULL, 0);
			if (r < 0) {
				perror("sysctl");
				exit(1);
			}
		}
		if (count) {
			if ((count & 15) == 1)
				printf("  elements iterations    lookups    inserts    deletes     splits\n");
			printf("%10jd %10jd %10jd %10jd %10jd %10jd\n",
				(intmax_t)(stats[0] - copy[0]),
				(intmax_t)(stats[1] - copy[1]),
				(intmax_t)(stats[2] - copy[2]),
				(intmax_t)(stats[3] - copy[3]),
				(intmax_t)(stats[4] - copy[4]),
				(intmax_t)(stats[5] - copy[5]));
		}
		nanosleep(&delay, NULL);
		bcopy(stats, copy, sizeof(stats));
	}
}


void
hammer_cmd_iostats(char **av, int ac)
{
	int mibs[9][16];
	size_t lens[9];
	int64_t stats[9];
	int64_t copy[9];
	size_t size;
	struct timespec delay = { 1, 0 };
	int count;
	int i;
	int r;

	if (ac > 0)
		loaddelay(&delay, av[0]);
	lens[0] = 16;
	lens[1] = 16;
	lens[2] = 16;
	lens[3] = 16;
	lens[4] = 16;
	lens[5] = 16;
	lens[6] = 16;
	lens[7] = 16;
	lens[8] = 16;
	r = 0;

	r |= sysctlnametomib("vfs.hammer.stats_file_read",
			     mibs[0], &lens[0]);
	r |= sysctlnametomib("vfs.hammer.stats_file_write",
			     mibs[1], &lens[1]);
	r |= sysctlnametomib("vfs.hammer.stats_disk_read",
			     mibs[2], &lens[2]);
	r |= sysctlnametomib("vfs.hammer.stats_disk_write",
			     mibs[3], &lens[3]);
	r |= sysctlnametomib("vfs.hammer.stats_file_iopsr",
			     mibs[4], &lens[4]);
	r |= sysctlnametomib("vfs.hammer.stats_file_iopsw",
			     mibs[5], &lens[5]);
	r |= sysctlnametomib("vfs.hammer.stats_inode_flushes",
			     mibs[6], &lens[6]);
	r |= sysctlnametomib("vfs.hammer.stats_commits",
			     mibs[7], &lens[7]);
	r |= sysctlnametomib("vfs.hammer.stats_undo",
			     mibs[8], &lens[8]);
	if (r < 0) {
		perror("sysctl: HAMMER stats not available");
		exit(1);
	}

	for (count = 0; ; ++count) {
		for (i = 0; i <= 8; ++i) {
			size = sizeof(stats[0]);
			r = sysctl(mibs[i], lens[i], &stats[i], &size, NULL, 0);
			if (r < 0) {
				perror("sysctl");
				exit(1);
			}
		}
		if (count) {
			if ((count & 15) == 1)
				printf("  file-rd   file-wr  dev-read dev-write inode_ops ino_flsh cmmit     undo\n");
			printf("%9jd %9jd %9jd %9jd %9jd %8jd %5jd %8jd\n",
				(intmax_t)(stats[0] - copy[0]),
				(intmax_t)(stats[1] - copy[1]),
				(intmax_t)(stats[2] - copy[2]),
				(intmax_t)(stats[3] - copy[3]),
				(intmax_t)(stats[4] + stats[5] -
					   copy[4] - copy[5]),
				(intmax_t)(stats[6] - copy[6]),
				(intmax_t)(stats[7] - copy[7]),
				(intmax_t)(stats[8] - copy[8]));
		}
		nanosleep(&delay, NULL);
		bcopy(stats, copy, sizeof(stats));
	}
}

/*
 * Convert a delay string (e.g. "0.1") into a timespec.
 */
static
void
loaddelay(struct timespec *ts, const char *arg)
{
	double d;

	d = strtod(arg, NULL);
	if (d < 0.001)
		d = 0.001;
	ts->tv_sec = (int)d;
	ts->tv_nsec = (int)(modf(d, &d) * 1000000000.0);
}

