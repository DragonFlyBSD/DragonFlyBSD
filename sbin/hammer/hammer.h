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
 * $DragonFly: src/sbin/hammer/hammer.h,v 1.27 2008/11/13 02:04:27 dillon Exp $
 */

#include <sys/types.h>
#include <sys/diskslice.h>
#include <sys/diskmbr.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/mount.h>
#include <sys/wait.h>

#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <stddef.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>
#include <fcntl.h>
#include <assert.h>
#include <err.h>
#include <ctype.h>
#include <signal.h>
#include <dirent.h>
#include <uuid.h>

#include "hammer_util.h"
#include <vfs/hammer/hammer_ioctl.h>

#include <libhammer.h>

struct softprune {
	struct softprune *next;
	struct statfs fs;
	char *filesystem;
	struct hammer_ioc_prune prune;
	int maxelms;
	int prune_min;
};

extern int RecurseOpt;
extern int VerboseOpt;
extern int QuietOpt;
extern int TwoWayPipeOpt;
extern int TimeoutOpt;
extern int DelayOpt;
extern char *SshPort;
extern int CompressOpt;
extern int ForceYesOpt;
extern int RunningIoctl;
extern int DidInterrupt;
extern int ForceOpt;
extern int BulkOpt;
extern u_int64_t BandwidthOpt;
extern u_int64_t SplitupOpt;
extern u_int64_t MemoryLimit;
extern const char *SplitupOptStr;
extern const char *LinkPath;
extern const char *CyclePath;

void hammer_cmd_show(hammer_tid_t node_offset, u_int32_t lo,
		int64_t obj_id, int depth,
		hammer_base_elm_t left_bound, hammer_base_elm_t right_bound);
void hammer_cmd_show_undo(void);
void hammer_cmd_sshremote(const char *cmd, const char *target);
void hammer_cmd_recover(const char *target_dir);
void hammer_cmd_checkmap(void);
void hammer_cmd_prune(char **av, int ac);
void hammer_cmd_softprune(char **av, int ac, int everything_opt);
void hammer_cmd_bstats(char **av, int ac);
void hammer_cmd_iostats(char **av, int ac);
void hammer_cmd_synctid(char **av, int ac);
void hammer_cmd_mirror_read(char **av, int ac, int streaming);
void hammer_cmd_mirror_write(char **av, int ac);
void hammer_cmd_mirror_copy(char **av, int ac, int streaming);
void hammer_cmd_mirror_dump(void);
void hammer_cmd_history(const char *offset_str, char **av, int ac);
void hammer_cmd_blockmap(void);
void hammer_cmd_reblock(char **av, int ac, int flags);
void hammer_cmd_rebalance(char **av, int ac);
void hammer_cmd_pseudofs_status(char **av, int ac);
void hammer_cmd_pseudofs_create(char **av, int ac, int is_slave);
void hammer_cmd_pseudofs_update(char **av, int ac);
void hammer_cmd_pseudofs_destroy(char **av, int ac);
void hammer_cmd_pseudofs_upgrade(char **av, int ac);
void hammer_cmd_pseudofs_downgrade(char **av, int ac);
void hammer_cmd_snapshot(char **av, int ac);
void hammer_cmd_snap(char **av, int ac, int tostdout, int fsbase);
void hammer_cmd_snapls(char **av, int ac);
void hammer_cmd_snaprm(char **av, int ac);
void hammer_cmd_cleanup(char **av, int ac);
void hammer_cmd_config(char **av, int ac);
void hammer_cmd_viconfig(char **av, int ac);
void hammer_cmd_info(char **av, int ac);
void hammer_cmd_get_version(char **av, int ac);
void hammer_cmd_set_version(char **av, int ac);
void hammer_cmd_volume_add(char **av, int ac);
void hammer_cmd_volume_del(char **av, int ac);
void hammer_cmd_volume_list(char **av, int ac);
void hammer_cmd_dedup_simulate(char **av, int ac);
void hammer_cmd_dedup(char **av, int ac);

void hammer_get_cycle(hammer_base_elm_t base, hammer_tid_t *tidp);
void hammer_set_cycle(hammer_base_elm_t base, hammer_tid_t tid);
void hammer_reset_cycle(void);

int getpfs(struct hammer_ioc_pseudofs_rw *pfs, char *path);
void relpfs(int fd, struct hammer_ioc_pseudofs_rw *pfs);
void hammer_check_restrict(const char *path);
void hammer_softprune_scandir(struct softprune **basep,
                         struct hammer_ioc_prune *template,
                         const char *dirname);
