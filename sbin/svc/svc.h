/*
 * Copyright (c) 2014 The DragonFly Project.  All rights reserved.
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
#include <sys/param.h>
#include <sys/procctl.h>
#include <sys/stat.h>
#include <sys/file.h>
#include <sys/time.h>
#include <sys/socket.h>
#include <sys/wait.h>
#include <sys/un.h>
#include <sys/tty.h>
#include <stdio.h>
#include <stdarg.h>
#include <stdlib.h>
#include <stddef.h>
#include <unistd.h>
#include <string.h>
#include <signal.h>
#include <dirent.h>
#include <assert.h>
#include <errno.h>
#include <ctype.h>
#include <pwd.h>
#include <grp.h>
#include <pthread.h>

typedef struct SvcCommand {
	int	mountdev : 1;
	int	cmdline : 1;
	int	foreground : 1;
	int	restart_some : 1;
	int	restart_all : 1;
	int	exit_mode : 1;
	int	sync_mode : 1;
	int	tail_mode : 3;
	int	jail_clean : 1;
	int	manual_stop : 1;
	int	empty_label : 1;	/* label not specified (vs 'all') */
	int	commanded : 1;		/* command by system operator */
	int	force_remove_files : 1;
	FILE	*fp;			/* nominal output */
	char	*piddir;
	char	*rootdir;
	char	*jaildir;
	char	*logfile;
	char	*proctitle;
	char	*directive;
	char	*label;
	char	**ext_av;
	int	ext_ac;
	char	**orig_av;
	int	orig_ac;
	int	restart_timo;
	int	termkill_timo;
	int	debug;
	int	restart_per;
	int	restart_count;
	struct passwd pwent;
	struct group grent;
	gid_t	groups[NGROUPS];
	int	ngroups;

	pthread_cond_t logcond;		/* wait for activity */
	char	logbuf[8192];		/* must be power of 2 >= 2048 */
	int	logwindex;
	int	logcount;
	int	logfds[2];		/* logfile pipe descriptors */
	int	logfd;			/* logfile file descriptor */
} command_t;

typedef enum {
	RS_STOPPED,	/* service died or stopped */
	RS_STARTED,	/* service running */
	RS_STOPPING1,	/* fresh pid to stop */
	RS_STOPPING2,	/* TERM sent */
	RS_STOPPING3,	/* KILL sent */
} runstate_t;

#define LOGCHUNK	1024

extern pthread_mutex_t serial_mtx;

int process_cmd(command_t *cmd, FILE *fp, int ac, char **av);
int execute_cmd(command_t *cmd);
void free_cmd(command_t *cmd);

void sfree(char **strp);
void sreplace(char **strp, const char *orig);
void sdup(char **strp);
void afree(char ***aryp);
void adup(char ***aryp);
int setup_pid_and_socket(command_t *cmd, int *lfdp, int *pfdp);
void remove_pid_and_socket(command_t *cmd, const char *label);

void remote_execute(command_t *cmd, const char *label);
void remote_listener(command_t *cmd, int lfd);
int remote_wait(void);

int execute_init(command_t *cmd);
int execute_start(command_t *cmd);
int execute_stop(command_t *cmd);
int execute_restart(command_t *cmd);
int execute_exit(command_t *cmd);
int execute_list(command_t *cmd);
int execute_status(command_t *cmd);
int execute_log(command_t *cmd);
int execute_logfile(command_t *cmd);
int execute_help(command_t *cmd);
