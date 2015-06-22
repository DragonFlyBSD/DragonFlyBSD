/*-
 * Copyright (c) 1991, 1993
 *	The Regents of the University of California.  All rights reserved.
 *
 * This code is derived from software contributed to Berkeley by
 * Kenneth Almquist.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Neither the name of the University nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE REGENTS AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE REGENTS OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 *	@(#)jobs.h	8.2 (Berkeley) 5/4/95
 * $FreeBSD$
 */

/* Mode argument to forkshell.  Don't change FORK_FG or FORK_BG. */
#define FORK_FG 0
#define FORK_BG 1
#define FORK_NOJOB 2

#include <signal.h>		/* for sig_atomic_t */

/*
 * A job structure contains information about a job.  A job is either a
 * single process or a set of processes contained in a pipeline.  In the
 * latter case, pidlist will be non-NULL, and will point to a -1 terminated
 * array of pids.
 */

struct procstat {
	pid_t pid;		/* process id */
	int status;		/* status flags (defined above) */
	char *cmd;		/* text of command being run */
};


/* states */
#define JOBSTOPPED 1		/* all procs are stopped */
#define JOBDONE 2		/* all procs are completed */


struct job {
	struct procstat ps0;	/* status of process */
	struct procstat *ps;	/* status or processes when more than one */
	short nprocs;		/* number of processes */
	pid_t pgrp;		/* process group of this job */
	char state;		/* true if job is finished */
	char used;		/* true if this entry is in used */
	char changed;		/* true if status has changed */
	char foreground;	/* true if running in the foreground */
	char remembered;	/* true if $! referenced */
#if JOBS
	char jobctl;		/* job running under job control */
	struct job *next;	/* job used after this one */
#endif
};

enum {
	SHOWJOBS_DEFAULT,	/* job number, status, command */
	SHOWJOBS_VERBOSE,	/* job number, PID, status, command */
	SHOWJOBS_PIDS,		/* PID only */
	SHOWJOBS_PGIDS		/* PID of the group leader only */
};

extern int job_warning;		/* user was warned about stopped jobs */

void setjobctl(int);
void showjobs(int, int);
struct job *makejob(union node *, int);
pid_t forkshell(struct job *, union node *, int);
pid_t vforkexecshell(struct job *, char **, char **, const char *, int, int []);
int waitforjob(struct job *, int *);
int stoppedjobs(void);
int backgndpidset(void);
pid_t backgndpidval(void);
char *commandtext(union node *);

#if ! JOBS
#define setjobctl(on)	/* do nothing */
#endif
