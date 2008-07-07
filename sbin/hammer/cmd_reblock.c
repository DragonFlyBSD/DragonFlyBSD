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
 * $DragonFly: src/sbin/hammer/cmd_reblock.c,v 1.10 2008/07/07 00:27:22 dillon Exp $
 */

#include "hammer.h"

static void reblock_usage(int exit_code);

/*
 * reblock <filesystem> [compaction_precentage] (default 90%)
 */
void
hammer_cmd_reblock(char **av, int ac, int flags)
{
	struct hammer_ioc_reblock reblock;
	const char *filesystem;
	int fd;
	int perc;

	if (TimeoutOpt > 0)
		alarm(TimeoutOpt);

	bzero(&reblock, sizeof(reblock));

	reblock.key_beg.localization = HAMMER_MIN_LOCALIZATION;
	reblock.key_beg.obj_id = HAMMER_MIN_OBJID;
	hammer_get_cycle(&reblock.key_beg, NULL);

	reblock.key_end.localization = HAMMER_MAX_LOCALIZATION;
	reblock.key_end.obj_id = HAMMER_MAX_OBJID;

	reblock.head.flags = flags & HAMMER_IOC_DO_FLAGS;

	/*
	 * Restrict the localization domain if asked to do inodes or data,
	 * but not both.
	 */
	switch(flags & (HAMMER_IOC_DO_INODES|HAMMER_IOC_DO_DATA|HAMMER_IOC_DO_DIRS)) {
	case HAMMER_IOC_DO_INODES:
		reblock.key_beg.localization = HAMMER_LOCALIZE_INODE;
		reblock.key_end.localization = HAMMER_LOCALIZE_INODE;
		break;
	case HAMMER_IOC_DO_DIRS:
	case HAMMER_IOC_DO_DATA:
		reblock.key_beg.localization = HAMMER_LOCALIZE_MISC;
		reblock.key_end.localization = HAMMER_LOCALIZE_MISC;
		break;
	}

	if (ac == 0)
		reblock_usage(1);
	filesystem = av[0];
	if (ac == 1) {
		perc = 100;
	} else {
		perc = strtol(av[1], NULL, 0);
		if (perc < 0 || perc > 100)
			reblock_usage(1);
	}
	reblock.free_level = (int)((int64_t)perc *
				   HAMMER_LARGEBLOCK_SIZE / 100);
	reblock.free_level = HAMMER_LARGEBLOCK_SIZE - reblock.free_level;
	if (reblock.free_level < 0)
		reblock.free_level = 0;
	printf("reblock free level %d\n", reblock.free_level);

	fd = open(filesystem, O_RDONLY);
	if (fd < 0)
		err(1, "Unable to open %s", filesystem);
	if (ioctl(fd, HAMMERIOC_REBLOCK, &reblock) < 0) {
		printf("Reblock %s failed: %s\n", filesystem, strerror(errno));
	} else if (reblock.head.flags & HAMMER_IOC_HEAD_INTR) {
		printf("Reblock %s interrupted by timer at %016llx %04x\n",
			filesystem,
			reblock.key_cur.obj_id,
			reblock.key_cur.localization);
		if (CyclePath) {
			hammer_set_cycle(&reblock.key_cur, 0);
		}
	} else {
		if (CyclePath)
			hammer_reset_cycle();
		printf("Reblock %s succeeded\n", filesystem);
	}
	close(fd);
	printf("Reblocked:\n"
	       "    %lld/%lld btree nodes\n"
	       "    %lld/%lld data elements\n"
	       "    %lld/%lld data bytes\n",
	       reblock.btree_moves, reblock.btree_count,
	       reblock.data_moves, reblock.data_count,
	       reblock.data_byte_moves, reblock.data_byte_count
	);
}

static
void
reblock_usage(int exit_code)
{
	fprintf(stderr, "hammer reblock <filesystem> [percentage]\n");
	fprintf(stderr, "hammer reblock-btree <filesystem> [percentage]\n");
	fprintf(stderr, "hammer reblock-inodes <filesystem> [percentage]\n");
	fprintf(stderr, "hammer reblock-dirs <filesystem> [percentage]\n");
	fprintf(stderr, "hammer reblock-data <filesystem> [percentage]\n");
	fprintf(stderr, "By default 90%% is used.  Use 100%% to defragment\n");
	exit(exit_code);
}

