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

#include <sys/sysctl.h>
#include <math.h>

#include "hammer.h"

static void loaddelay(struct timespec *ts, const char *arg);

#define _HAMMER "vfs.hammer.stats_"
#define bstats_title	\
"   lookups   searches    inserts    deletes   elements     splits iterations  rootiters   reciters"
#define iostats_title	\
"   f_read   f_write   f_iopsr   f_iopsw    d_read   d_write i_flushes   commits      undo      redo"

/*
 * Taken from sys/vfs/hammer/hammer_vfsops.c
 */
struct btree_stats {
	int64_t btree_lookups;
	int64_t btree_searches;
	int64_t btree_inserts;
	int64_t btree_deletes;
	int64_t btree_elements;
	int64_t btree_splits;
	int64_t btree_iterations;
	int64_t btree_root_iterations;
	int64_t record_iterations;
};

struct io_stats {
	int64_t file_read;
	int64_t file_write;
	int64_t file_iopsr;
	int64_t file_iopsw;
	int64_t disk_read;
	int64_t disk_write;
	int64_t inode_flushes;
	int64_t commits;
	int64_t undo;
	int64_t redo;
};

static __inline __always_inline
int
_sysctl(const char *name, int64_t *p)
{
	size_t len = sizeof(*p);
	return(sysctlbyname(name, p, &len, NULL, 0));
}

static __inline __always_inline
void
collect_bstats(struct btree_stats *p)
{
	/* sysctls must exist, so ignore return values */
	_sysctl(_HAMMER"btree_lookups", &p->btree_lookups);
	_sysctl(_HAMMER"btree_searches", &p->btree_searches);
	_sysctl(_HAMMER"btree_inserts", &p->btree_inserts);
	_sysctl(_HAMMER"btree_deletes", &p->btree_deletes);
	_sysctl(_HAMMER"btree_elements", &p->btree_elements);
	_sysctl(_HAMMER"btree_splits", &p->btree_splits);
	_sysctl(_HAMMER"btree_iterations", &p->btree_iterations);
	_sysctl(_HAMMER"btree_root_iterations", &p->btree_root_iterations);
	_sysctl(_HAMMER"record_iterations", &p->record_iterations);
}

static __inline __always_inline
void
collect_iostats(struct io_stats *p)
{
	/* sysctls must exist, so ignore return values */
	_sysctl(_HAMMER"file_read", &p->file_read);
	_sysctl(_HAMMER"file_write", &p->file_write);
	_sysctl(_HAMMER"file_iopsr", &p->file_iopsr);
	_sysctl(_HAMMER"file_iopsw", &p->file_iopsw);
	_sysctl(_HAMMER"disk_read", &p->disk_read);
	_sysctl(_HAMMER"disk_write", &p->disk_write);
	_sysctl(_HAMMER"inode_flushes", &p->inode_flushes);
	_sysctl(_HAMMER"commits", &p->commits);
	_sysctl(_HAMMER"undo", &p->undo);
	_sysctl(_HAMMER"redo", &p->redo);
}

static __inline __always_inline
void
print_bstats(const struct btree_stats *p1, const struct btree_stats *p2)
{
	printf("%10jd %10jd %10jd %10jd %10jd %10jd %10jd %10jd %10jd",
		(intmax_t)(p1->btree_lookups - p2->btree_lookups),
		(intmax_t)(p1->btree_searches - p2->btree_searches),
		(intmax_t)(p1->btree_inserts - p2->btree_inserts),
		(intmax_t)(p1->btree_deletes - p2->btree_deletes),
		(intmax_t)(p1->btree_elements - p2->btree_elements),
		(intmax_t)(p1->btree_splits - p2->btree_splits),
		(intmax_t)(p1->btree_iterations - p2->btree_iterations),
		(intmax_t)(p1->btree_root_iterations - p2->btree_root_iterations),
		(intmax_t)(p1->record_iterations - p2->record_iterations));
		/* no trailing \n */
}

static __inline __always_inline
void
print_iostats(const struct io_stats *p1, const struct io_stats *p2)
{
	printf("%9jd %9jd %9jd %9jd %9jd %9jd %9jd %9jd %9jd %9jd",
		(intmax_t)(p1->file_read - p2->file_read),
		(intmax_t)(p1->file_write - p2->file_write),
		(intmax_t)(p1->file_iopsr - p2->file_iopsr),
		(intmax_t)(p1->file_iopsw - p2->file_iopsw),
		(intmax_t)(p1->disk_read - p2->disk_read),
		(intmax_t)(p1->disk_write - p2->disk_write),
		(intmax_t)(p1->inode_flushes - p2->inode_flushes),
		(intmax_t)(p1->commits - p2->commits),
		(intmax_t)(p1->undo - p2->undo),
		(intmax_t)(p1->redo - p2->redo));
		/* no trailing \n */
}

void
hammer_cmd_bstats(char **av, int ac)
{
	struct btree_stats st1, st2;
	struct timespec delay = {1, 0};
	int count;

	bzero(&st1, sizeof(st1));
	bzero(&st2, sizeof(st2));

	if (ac > 0)
		loaddelay(&delay, av[0]);

	for (count = 0; ; ++count) {
		collect_bstats(&st1);
		if (count) {
			if ((count & 15) == 1)
				printf(bstats_title"\n");
			print_bstats(&st1, &st2);
			printf("\n");
		}
		bcopy(&st1, &st2, sizeof(st2));
		nanosleep(&delay, NULL);
	}
}

void
hammer_cmd_iostats(char **av, int ac)
{
	struct io_stats st1, st2;
	struct timespec delay = {1, 0};
	int count;

	bzero(&st1, sizeof(st1));
	bzero(&st2, sizeof(st2));

	if (ac > 0)
		loaddelay(&delay, av[0]);

	for (count = 0; ; ++count) {
		collect_iostats(&st1);
		if (count) {
			if ((count & 15) == 1)
				printf(iostats_title"\n");
			print_iostats(&st1, &st2);
			printf("\n");
		}
		bcopy(&st1, &st2, sizeof(st2));
		nanosleep(&delay, NULL);
	}
}

void
hammer_cmd_stats(char **av, int ac)
{
	struct btree_stats bst1, bst2;
	struct io_stats ist1, ist2;
	struct timespec delay = {1, 0};
	int count;

	bzero(&bst1, sizeof(bst1));
	bzero(&bst2, sizeof(bst2));
	bzero(&ist1, sizeof(ist1));
	bzero(&ist2, sizeof(ist2));

	if (ac > 0)
		loaddelay(&delay, av[0]);

	for (count = 0; ; ++count) {
		collect_bstats(&bst1);
		collect_iostats(&ist1);
		if (count) {
			if ((count & 15) == 1) {
				printf(bstats_title"\t"iostats_title"\n");
			}
			print_bstats(&bst1, &bst2);
			printf("\t");
			print_iostats(&ist1, &ist2);
			printf("\n");
		}
		bcopy(&bst1, &bst2, sizeof(bst2));
		bcopy(&ist1, &ist2, sizeof(ist2));
		nanosleep(&delay, NULL);
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
