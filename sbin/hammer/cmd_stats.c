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

#include <math.h>

#include "hammer.h"


static void loaddelay(struct timespec *ts, const char *arg);

void
hammer_cmd_bstats(char **av, int ac)
{
	struct libhammer_btree_stats bs, bsc;
	struct timespec delay = { 1, 0 };
	int count;

	if (ac > 0)
		loaddelay(&delay, av[0]);

	bzero(&bsc, sizeof(bsc));
	for (count = 0; ; ++count) {
		if (libhammer_btree_stats(&bs) < 0)
			err(1, "Failed to get information from HAMMER sysctls");
		if (count) {
			if ((count & 15) == 1)
				printf("  elements iterations    lookups    "
				    "inserts    deletes     splits\n");
			printf("%10jd %10jd %10jd %10jd %10jd %10jd\n",
			    (intmax_t)(bs.elements - bsc.elements),
			    (intmax_t)(bs.iterations - bsc.iterations),
			    (intmax_t)(bs.lookups - bsc.lookups),
			    (intmax_t)(bs.inserts - bsc.inserts),
			    (intmax_t)(bs.deletes - bsc.deletes),
			    (intmax_t)(bs.splits - bsc.splits));
		}
		bcopy(&bs, &bsc, sizeof(bs));
		nanosleep(&delay, NULL);
	}
}

void
hammer_cmd_iostats(char **av, int ac)
{
	struct libhammer_io_stats ios, iosc;
	struct timespec delay = { 1, 0 };
	int64_t tiops = 0;
	int count;

	if (ac > 0)
		loaddelay(&delay, av[0]);

	bzero(&iosc, sizeof(iosc));
	for (count = 0; ; ++count) {
		if (libhammer_io_stats(&ios) < 0)
			err(1, "Failed to get information from HAMMER sysctls");
		tiops = (ios.file_iop_writes + ios.file_iop_reads) -
		    (iosc.file_iop_writes + iosc.file_iop_reads);
		if (count) {
			if ((count & 15) == 1)
				printf("  file-rd   file-wr  dev-read dev-write"
				    " inode_ops ino_flush    commit      undo\n");
			printf("%9jd %9jd %9jd %9jd %9jd %9jd %9jd %9jd\n",
			    (intmax_t)(ios.file_reads - iosc.file_reads),
			    (intmax_t)(ios.file_writes - iosc.file_writes),
			    (intmax_t)(ios.dev_reads - iosc.dev_reads),
			    (intmax_t)(ios.dev_writes - iosc.dev_writes),
			    (intmax_t)tiops,
			    (intmax_t)(ios.inode_flushes - iosc.inode_flushes),
			    (intmax_t)(ios.commits - iosc.commits),
			    (intmax_t)(ios.undo - iosc.undo));
		}
		nanosleep(&delay, NULL);
		bcopy(&ios, &iosc, sizeof(ios));
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
