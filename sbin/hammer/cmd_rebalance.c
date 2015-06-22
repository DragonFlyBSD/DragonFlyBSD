/*
 * Copyright (c) 2009 The DragonFly Project.  All rights reserved.
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

#include "hammer.h"

static void rebalance_usage(int exit_code);

/*
 * rebalance <filesystem> [saturation_percentage] (default 85%)
 */
void
hammer_cmd_rebalance(char **av, int ac)
{
	struct hammer_ioc_rebalance rebal;
	const char *filesystem;
	int fd;
	int perc;

	if (TimeoutOpt > 0)
		alarm(TimeoutOpt);

	bzero(&rebal, sizeof(rebal));

	rebal.key_beg.localization = HAMMER_MIN_LOCALIZATION;
	rebal.key_beg.obj_id = HAMMER_MIN_OBJID;
	hammer_get_cycle(&rebal.key_beg, NULL);

	rebal.key_end.localization = HAMMER_MAX_LOCALIZATION;
	rebal.key_end.obj_id = HAMMER_MAX_OBJID;
	rebal.allpfs = AllPFS;

	if (ac == 0)
		rebalance_usage(1);
	filesystem = av[0];
	if (ac == 1) {
		perc = 85;
	} else {
		perc = strtol(av[1], NULL, 0);
		if (perc < 50 || perc > 100)
			rebalance_usage(1);
	}
	rebal.saturation = HAMMER_BTREE_INT_ELMS * perc / 100;

	printf("rebalance start %016jx:%04x\n",
		(uintmax_t)rebal.key_beg.obj_id,
		rebal.key_beg.localization);

	fd = open(filesystem, O_RDONLY);
	if (fd < 0)
		err(1, "Unable to open %s", filesystem);
	RunningIoctl = 1;
	if (ioctl(fd, HAMMERIOC_REBALANCE, &rebal) < 0) {
		printf("Rebalance %s failed: %s\n",
		       filesystem, strerror(errno));
	} else if (rebal.head.flags & HAMMER_IOC_HEAD_INTR) {
		printf("Rebalance %s interrupted by timer at %016jx:%04x\n",
			filesystem,
			(uintmax_t)rebal.key_cur.obj_id,
			rebal.key_cur.localization);
		if (CyclePath) {
			hammer_set_cycle(&rebal.key_cur, 0);
		}
	} else {
		if (CyclePath)
			hammer_reset_cycle();
		printf("Rebalance %s succeeded\n", filesystem);
	}
	RunningIoctl = 0;
	close(fd);
	printf("Rebalance:\n"
	       "    %jd btree nodes scanned\n"
	       "    %jd btree nodes deleted\n"
	       "    %jd collision retries\n"
	       "    %jd btree nodes rebalanced\n",
	       (intmax_t)rebal.stat_ncount,
	       (intmax_t)rebal.stat_deletions,
	       (intmax_t)rebal.stat_collisions,
	       (intmax_t)rebal.stat_nrebal
	);
}

static
void
rebalance_usage(int exit_code)
{
	fprintf(stderr,
		"hammer rebalance <filesystem> [saturation_percentage]\n"
		"saturation_percentage is 50%%-100%%, default is 85%%.\n");
	exit(exit_code);
}
