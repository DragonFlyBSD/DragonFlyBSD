/*-
 * Copyright (c) 1988, 1989, 1990, 1993
 *	The Regents of the University of California.  All rights reserved.
 * Copyright (c) 1988, 1989 by Adam de Boor
 * Copyright (c) 1989 by Berkeley Softworks
 * All rights reserved.
 *
 * This code is derived from software contributed to Berkeley by
 * Adam de Boor.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. All advertising materials mentioning features or use of this software
 *    must display the following acknowledgement:
 *	This product includes software developed by the University of
 *	California, Berkeley and its contributors.
 * 4. Neither the name of the University nor the names of its contributors
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
 * @(#)job.c	8.2 (Berkeley) 3/19/94
 * $FreeBSD: src/usr.bin/make/job.c,v 1.75 2005/02/10 14:32:14 harti Exp $
 */

#ifndef OLD_JOKE
#define	OLD_JOKE 1
#endif /* OLD_JOKE */

/**
 * job.c
 *	handle the creation etc. of our child processes.
 *
 * Interface:
 *	Job_Make	Start the creation of the given target.
 *
 *	Job_CatchChildren
 *			Check for and handle the termination of any children.
 *			This must be called reasonably frequently to keep the
 *			whole make going at a decent clip, since job table
 *			entries aren't removed until their process is caught
 *			this way. Its single argument is true if the function
 *			should block waiting for a child to terminate.
 *
 *	Job_CatchOutput	Print any output our children have produced. Should
 *			also be called fairly frequently to keep the user
 *			informed of what's going on. If no output is waiting,
 *			it will block for a time given by the SEL_* constants,
 *			below, or until output is ready.
 *
 *	Job_Init	Called to initialize this module. in addition, any
 *			commands attached to the .BEGIN target are executed
 *			before this function returns. Hence, the makefile must
 *			have been parsed before this function is called.
 *
 *	Job_Full	Return true if the job table is filled.
 *
 *	Job_Empty	Return true if the job table is completely empty.
 *
 *	Job_Finish	Perform any final processing which needs doing. This
 *			includes the execution of any commands which have
 *			been/were attached to the .END target. It should only
 *			be called when the job table is empty.
 *
 *	Job_AbortAll	Abort all currently running jobs. It doesn't handle
 *			output or do anything for the jobs, just kills them.
 *			It should only be called in an emergency, as it were.
 *
 *	JobCheckCommands
 *			Verify that the commands for a target are ok. Provide
 *			them if necessary and possible.
 *
 *	JobTouch	Update a target without really updating it.
 *
 *	Job_Wait	Wait for all currently-running jobs to finish.
 *
 * compat.c
 *	The routines in this file implement the full-compatibility
 *	mode of PMake. Most of the special functionality of PMake
 *	is available in this mode. Things not supported:
 *	    - different shells.
 *	    - friendly variable substitution.
 *
 * Interface:
 *	Compat_Run	    Initialize things for this module and recreate
 *			    thems as need creatin'
 */

#include <sys/queue.h>
#include <sys/types.h>
#include <sys/select.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <ctype.h>
#include <err.h>
#include <errno.h>
#include <fcntl.h>
#include <inttypes.h>
#include <string.h>
#include <signal.h>
#include <stdlib.h>
#include <unistd.h>
#include <utime.h>

#include "arch.h"
#include "buf.h"
#include "config.h"
#include "dir.h"
#include "globals.h"
#include "GNode.h"
#include "job.h"
#include "make.h"
#include "parse.h"
#include "pathnames.h"
#include "proc.h"
#include "shell.h"
#include "str.h"
#include "suff.h"
#include "targ.h"
#include "util.h"
#include "var.h"

#define	TMPPAT	"/tmp/makeXXXXXXXXXX"
#define	MKLVL_MAXVAL	500
#define	MKLVL_ENVVAR	"__MKLVL__"

/*
 * The SEL_ constants determine the maximum amount of time spent in select
 * before coming out to see if a child has finished. SEL_SEC is the number of
 * seconds and SEL_USEC is the number of micro-seconds
 */
#define	SEL_SEC		2
#define	SEL_USEC	0

/*
 * Job Table definitions.
 *
 * The job "table" is kept as a linked Lst in 'jobs', with the number of
 * active jobs maintained in the 'nJobs' variable. At no time will this
 * exceed the value of 'maxJobs', initialized by the Job_Init function.
 *
 * When a job is finished, the Make_Update function is called on each of the
 * parents of the node which was just remade. This takes care of the upward
 * traversal of the dependency graph.
 */
#define	JOB_BUFSIZE	1024
typedef struct Job {
	struct Shell	*shell;
	pid_t		pid;	/* The child's process ID */

	struct GNode	*node;	/* The target the child is making */

	/*
	 * A LstNode for the first command to be saved after the job completes.
	 * This is NULL if there was no "..." in the job's commands.
	 */
	LstNode		*tailCmds;

	/*
	 * An FILE* for writing out the commands. This is only
	 * used before the job is actually started.
	 */
	FILE		*cmdFILE;

	/*
	 * A word of flags which determine how the module handles errors,
	 * echoing, etc. for the job
	 */
	short		flags;	/* Flags to control treatment of job */
#define	JOB_IGNERR	0x001	/* Ignore non-zero exits */
#define	JOB_SILENT	0x002	/* no output */
#define	JOB_SPECIAL	0x004	/* Target is a special one. i.e. run it locally
				 * if we can't export it and maxLocal is 0 */
#define	JOB_IGNDOTS	0x008	/* Ignore "..." lines when processing
				 * commands */
#define	JOB_FIRST	0x020	/* Job is first job for the node */
#define	JOB_RESTART	0x080	/* Job needs to be completely restarted */
#define	JOB_RESUME	0x100	/* Job needs to be resumed b/c it stopped,
				 * for some reason */
#define	JOB_CONTINUING	0x200	/* We are in the process of resuming this job.
				 * Used to avoid infinite recursion between
				 * JobFinish and JobRestart */

	/* union for handling shell's output */
	union {
		/*
		 * This part is used when usePipes is true.
		 * The output is being caught via a pipe and the descriptors
		 * of our pipe, an array in which output is line buffered and
		 * the current position in that buffer are all maintained for
		 * each job.
		 */
		struct {
			/*
			 * Input side of pipe associated with
			 * job's output channel
			 */
			int	op_inPipe;

			/*
			 * Output side of pipe associated with job's
			 * output channel
			 */
			int	op_outPipe;

			/*
			 * Buffer for storing the output of the
			 * job, line by line
			 */
			char	op_outBuf[JOB_BUFSIZE + 1];

			/* Current position in op_outBuf */
			int	op_curPos;
		}	o_pipe;

		/*
		 * If usePipes is false the output is routed to a temporary
		 * file and all that is kept is the name of the file and the
		 * descriptor open to the file.
		 */
		struct {
			/* Name of file to which shell output was rerouted */
			char	of_outFile[sizeof(TMPPAT)];

			/*
			 * Stream open to the output file. Used to funnel all
			 * from a single job to one file while still allowing
			 * multiple shell invocations
			 */
			int	of_outFd;
		}	o_file;

	} output;	/* Data for tracking a shell's output */

	TAILQ_ENTRY(Job) link;	/* list link */
} Job;

#define	outPipe		output.o_pipe.op_outPipe
#define	inPipe		output.o_pipe.op_inPipe
#define	outBuf		output.o_pipe.op_outBuf
#define	curPos		output.o_pipe.op_curPos

#define	outFile		output.o_file.of_outFile
#define	outFd		output.o_file.of_outFd

TAILQ_HEAD(JobList, Job);

/*
 * error handling variables
 */
static int	errors = 0;	/* number of errors reported */
static int	aborting = 0;	/* why is the make aborting? */
#define	ABORT_ERROR	1	/* Because of an error */
#define	ABORT_INTERRUPT	2	/* Because it was interrupted */
#define	ABORT_WAIT	3	/* Waiting for jobs to finish */

/*
 * post-make command processing. The node postCommands is really just the
 * .END target but we keep it around to avoid having to search for it
 * all the time.
 */
static GNode	*postCommands;

/*
 * The number of commands actually printed for a target. Should this
 * number be 0, no shell will be executed.
 */
static int	numCommands;

/*
 * Return values from JobStart.
 */
#define	JOB_RUNNING	0	/* Job is running */
#define	JOB_ERROR	1	/* Error in starting the job */
#define	JOB_FINISHED	2	/* The job is already finished */
#define	JOB_STOPPED	3	/* The job is stopped */

/*
 * The maximum number of jobs that may run. This is initialize from the
 * -j argument for the leading make and from the FIFO for sub-makes.
 */
static int	maxJobs;
static int	nJobs;		/* The number of children currently running */

/* The structures that describe them */
static struct JobList jobs = TAILQ_HEAD_INITIALIZER(jobs);

static bool	jobFull;	/* Flag to tell when the job table is full. It
				 * is set true when (1) the total number of
				 * running jobs equals the maximum allowed */
static fd_set	outputs;	/* Set of descriptors of pipes connected to
				 * the output channels of children */
static GNode	*lastNode;	/* The node for which output was most recently
				 * produced. */
static const char *targFmt;	/* Format string to use to head output from a
				 * job when it's not the most-recent job heard
				 * from */

#define	TARG_FMT  "--- %s ---\n" /* Default format */
#define	MESSAGE(fp, gn) \
	 fprintf(fp, targFmt, gn->name);

/*
 * When JobStart attempts to run a job but isn't allowed to
 * or when Job_CatchChildren detects a job that has
 * been stopped somehow, the job is placed on the stoppedJobs queue to be run
 * when the next job finishes.
 *
 * Lst of Job structures describing jobs that were stopped due to
 * concurrency limits or externally
 */
static struct JobList stoppedJobs = TAILQ_HEAD_INITIALIZER(stoppedJobs);

static int	fifoFd;		/* Fd of our job fifo */
static char	fifoName[] = "/tmp/make_fifo_XXXXXXXXX";
static int	fifoMaster;

#if defined(USE_PGRP)
# if defined(SYSV)
#  define	KILL(pid, sig)		killpg(-(pid), (sig))
# else
#  define	KILL(pid, sig)		killpg((pid), (sig))
# endif
#else
# define	KILL(pid, sig)		kill((pid), (sig))
#endif

/*
 * Grmpf... There is no way to set bits of the wait structure
 * anymore with the stupid W*() macros. I liked the union wait
 * stuff much more. So, we devise our own macros... This is
 * really ugly, use dramamine sparingly. You have been warned.
 */
#define	W_SETMASKED(st, val, fun)				\
	{							\
		int sh = (int)~0;				\
		int mask = fun(sh);				\
								\
		for (sh = 0; ((mask >> sh) & 1) == 0; sh++)	\
			continue;				\
		*(st) = (*(st) & ~mask) | ((val) << sh);	\
	}

#define	W_SETTERMSIG(st, val) W_SETMASKED(st, val, WTERMSIG)
#define	W_SETEXITSTATUS(st, val) W_SETMASKED(st, val, WEXITSTATUS)

typedef void AbortProc(const char [], ...) __printflike(1, 2);

static void JobRestart(Job *);
static int JobStart(GNode *, int, Job *);
static void JobDoOutput(Job *, bool);
static void JobRestartJobs(void);
static int Compat_RunCommand(GNode *, const char [], GNode *);
static void JobPassSig(int);
static void JobTouch(GNode *, bool);
static bool JobCheckCommands(GNode *, AbortProc *);

static GNode	    *curTarg = NULL;

static volatile sig_atomic_t got_SIGINT;
static volatile sig_atomic_t got_SIGHUP;
static volatile sig_atomic_t got_SIGQUIT;
static volatile sig_atomic_t got_SIGTERM;
#if defined(USE_PGRP) 
static volatile sig_atomic_t got_SIGTSTP;
static volatile sig_atomic_t got_SIGTTOU;
static volatile sig_atomic_t got_SIGTTIN;
static volatile sig_atomic_t got_SIGWINCH;
#endif

Shell	*commandShell = NULL;

/**
 * In lieu of a good way to prevent every possible looping in make(1), stop
 * there from being more than MKLVL_MAXVAL processes forked by make(1), to
 * prevent a forkbomb from happening, in a dumb and mechanical way.
 *
 * Side Effects:
 *	Creates or modifies environment variable MKLVL_ENVVAR via setenv().
 */
static void
check_make_level(void)
{
	char	*value = getenv(MKLVL_ENVVAR);
	int	level = (value == NULL) ? 0 : atoi(value);

	if (level < 0) {
		errc(2, EAGAIN, "Invalid value for recursion level (%d).",
		    level);
	} else if (level > MKLVL_MAXVAL) {
		errc(2, EAGAIN, "Max recursion level (%d) exceeded.",
		    MKLVL_MAXVAL);
	} else {
		char new_value[32];
		sprintf(new_value, "%d", level + 1);
		if (setenv(MKLVL_ENVVAR, new_value, 1) == -1)
			Punt("setenv: %s: can't allocate memory", MKLVL_ENVVAR);
	}
}

/**
 * Handle recept of a signal by setting a variable. The handling action is
 * defered until the mainline code can safely handle it.
 */
static void
SigCatcher(int sig)
{
	switch (sig) {
	case SIGCHLD:
		/* do nothing */
		break;
	case SIGINT:
		got_SIGINT++;
		break;
	case SIGHUP:
		got_SIGHUP++;
		break;
	case SIGQUIT:
		got_SIGQUIT++;
		break;
	case SIGTERM:
		got_SIGTERM++;
		break;
#if defined(USE_PGRP)
	case SIGTSTP:
		got_SIGTSTP++;
		break;
	case SIGTTOU:
		got_SIGTTOU++;
		break;
	case SIGTTIN:
		got_SIGTTIN++;
		break;
	case SIGWINCH:
		got_SIGWINCH++;
		break;
#endif
	default:
		/* unexpected signal */
		break;
	}
}

/**
 *
 */
static void
SigHandler(void)
{
	if (got_SIGINT) {
		got_SIGINT = 0;
		JobPassSig(SIGINT);
	}
	if (got_SIGHUP) {
		got_SIGHUP = 0;
		JobPassSig(SIGHUP);
	}
	if (got_SIGQUIT) {
		got_SIGQUIT = 0;
		JobPassSig(SIGQUIT);
	}
	if (got_SIGTERM) {
		got_SIGTERM = 0;
		JobPassSig(SIGTERM);
	}
#if defined(USE_PGRP)
	if (got_SIGTSTP) {
		got_SIGTSTP = 0;
		JobPassSig(SIGTSTP);
	}
	if (got_SIGTTOU) {
		got_SIGTTOU = 0;
		JobPassSig(SIGTTOU);
	}
	if (got_SIGTTIN) {
		got_SIGTTIN = 0;
		JobPassSig(SIGTTIN);
	}
	if (got_SIGWINCH) {
		got_SIGWINCH = 0;
		JobPassSig(SIGWINCH);
	}
#endif
}

static
sig_t
getsignal(int signo)
{
	struct sigaction sa;
	if (sigaction(signo, NULL, &sa) < 0)
		sa.sa_handler = SIG_IGN;
	return(sa.sa_handler);
}

void
Sig_Init(bool compat)
{
	struct sigaction	sa;

	got_SIGINT = 0;
	got_SIGHUP = 0;
	got_SIGQUIT = 0;
	got_SIGTERM = 0;
#if defined(USE_PGRP)
	got_SIGTSTP = 0;
	got_SIGTTOU = 0;
	got_SIGTTIN = 0;
	got_SIGWINCH = 0;
#endif

	if (compat == false) {
		/*
		 * Setup handler to catch SIGCHLD so that we get kicked out
		 * of select() when we need to look at a child.  This is only
		 * known to matter for the -j case (perhaps without -P).
		 */
		sigemptyset(&sa.sa_mask);
		sa.sa_handler = SigCatcher;
		sa.sa_flags = SA_NOCLDSTOP;
		sigaction(SIGCHLD, &sa, NULL);
	}

	/*
	 * Catch the four signals that POSIX specifies if they aren't
	 * ignored.
	 */
	sigemptyset(&sa.sa_mask);
	sa.sa_handler = SigCatcher;
	sa.sa_flags = 0;

	if (getsignal(SIGINT) != SIG_IGN) {
		sigaction(SIGINT, &sa, NULL);
	}
	if (getsignal(SIGHUP) != SIG_IGN) {
		sigaction(SIGHUP, &sa, NULL);
	}
	if (getsignal(SIGQUIT) != SIG_IGN) {
		sigaction(SIGQUIT, &sa, NULL);
	}
	if (getsignal(SIGTERM) != SIG_IGN) {
		sigaction(SIGTERM, &sa, NULL);
	}

#if defined(USE_PGRP)
	if (compat == false) {
		/*
		 * There are additional signals that need to be caught and
		 * passed if either the export system wants to be told
		 * directly of signals or if we're giving each job its own
		 * process group (since then it won't get signals from the
		 * terminal driver as we own the terminal)
		 */
		if (getsignal(SIGTSTP) != SIG_IGN) {
			sigaction(SIGTSTP, &sa, NULL);
		}
		if (getsignal(SIGTTOU) != SIG_IGN) {
			sigaction(SIGTTOU, &sa, NULL);
		}
		if (getsignal(SIGTTIN) != SIG_IGN) {
			sigaction(SIGTTIN, &sa, NULL);
		}
		if (getsignal(SIGWINCH) != SIG_IGN) {
			sigaction(SIGWINCH, &sa, NULL);
		}
	}
#endif
}

/**
 */
void
Proc_Init(void)
{
	check_make_level();

#ifdef RLIMIT_NOFILE
	{
		struct rlimit		rl;

		/*
		 * get rid of resource limit on file descriptors
		 */
		if (getrlimit(RLIMIT_NOFILE, &rl) == -1) {
			err(2, "getrlimit");
		}
		rl.rlim_cur = rl.rlim_max;
		if (setrlimit(RLIMIT_NOFILE, &rl) == -1) {
			err(2, "setrlimit");
		}
	}
#endif
}

/**
 * Wait for child process to terminate.
 */
static int
ProcWait(ProcStuff *ps)
{
	/*
	 * Wait for the process to exit.
	 */
	for (;;) {
		pid_t	pid;

		pid = waitpid(ps->child_pid, &ps->child_status, 0);
		if (pid == ps->child_pid) {
			/*
			 * We finished waiting for the child.
			 */
			return (0);
		} else {
			if (errno == EINTR) {
				/*
				 * Return so we can handle the signal that
				 * was delivered.
				 */
				return (-1);
			} else {
				Fatal("error in wait: %d", pid);
				/* NOTREACHED */
			}
		}
	}
}

/**
 * Execute the list of command associated with the node.
 *
 * @param save	Commands preceeded by "..." are save in this node to be
 *		executed after the other rules are executed.
 */
static void
Compat_RunCmds(GNode *gn, GNode *save)
{
	Lst	*cmds = &gn->commands;
	LstNode	*ln;

	LST_FOREACH(ln, cmds) {
		char	*cmd = Lst_Datum(ln);

		if (Compat_RunCommand(gn, cmd, save))
			break;
	}
}

/**
 * Pass a signal on to all jobs.
 *
 * Side Effects:
 *	We die by the same signal.
 */
static void
JobPassSig(int signo)
{
	Job	*job;

	DEBUGF(JOB, ("JobPassSig(%d) called.\n", signo));

	/*
	 * Propagate signal to children and in addition, send SIGCONT
	 * in case any of the children where suspended, so the the
	 * signal will get delivered.
	 */
	TAILQ_FOREACH(job, &jobs, link) {
		DEBUGF(JOB, ("JobPassSig passing signal %d to child %jd.\n",
		    signo, (intmax_t)job->pid));
		KILL(job->pid, signo);
		KILL(job->pid, SIGCONT);
	}

#if defined(USE_PGRP)
	/*
	 * Why are we even catching these signals?
	 * SIGTSTP, SIGTTOU, SIGTTIN, and SIGWINCH
	 */
	switch (signo) {
	case SIGTSTP:
	case SIGTTOU:
	case SIGTTIN:
	case SIGWINCH:
		return;
	default:
		break;
	}
#endif

	/*
	 * Deal with proper cleanup based on the signal received. We only run
	 * the .INTERRUPT target if the signal was in fact an interrupt.
	 * The other three termination signals are more of a "get out *now*"
	 * command.
	 *
	 */
	aborting = ABORT_INTERRUPT;

	TAILQ_FOREACH(job, &jobs, link) {
		if (!Targ_Precious(job->node)) {
			char *file = (job->node->path == NULL ?
			    job->node->name : job->node->path);

			if (!noExecute && eunlink(file) != -1) {
				Error("*** %s removed", file);
			}
		}
	}

	if ((signo == SIGINT) && !touchFlag) {
		GNode	*interrupt;

		interrupt = Targ_FindNode(".INTERRUPT", TARG_NOCREATE);
		if (interrupt != NULL) {
			ignoreErrors = false;

			JobStart(interrupt, JOB_IGNDOTS, NULL);
			while (nJobs) {
				Job_CatchOutput(0);
				Job_CatchChildren(!usePipes);
			}
		}
	}

	/*
	 * Leave gracefully if SIGQUIT, rather than core dumping.
	 */
	if (signo == SIGQUIT) {
		signo = SIGINT;
	}

	DEBUGF(JOB, ("JobPassSig passing signal %d to self.\n", signo));

	signal(signo, SIG_DFL);
	KILL(getpid(), signo);
}

/**
 * JobPrintCommand
 *	Put out another command for the given job. If the command starts
 *	with an @ or a - we process it specially. In the former case,
 *	so long as the -s and -n flags weren't given to make, we stick
 *	a shell-specific echoOff command in the script. In the latter,
 *	we ignore errors for the entire job, unless the shell has error
 *	control.
 *	If the command is just "..." we take all future commands for this
 *	job to be commands to be executed once the entire graph has been
 *	made and return non-zero to signal that the end of the commands
 *	was reached. These commands are later attached to the postCommands
 *	node and executed by Job_Finish when all things are done.
 *	This function is called from JobStart via LST_FOREACH.
 *
 * Results:
 *	Always 0, unless the command was "..."
 *
 * Side Effects:
 *	If the command begins with a '-' and the shell has no error control,
 *	the JOB_IGNERR flag is set in the job descriptor.
 *	If the command is "..." and we're not ignoring such things,
 *	tailCmds is set to the successor node of the cmd.
 *	numCommands is incremented if the command is actually printed.
 */
static int
JobPrintCommand(char *cmd, Job *job)
{
	struct Shell	*shell = job->shell;
	bool	noSpecials;	/* true if we shouldn't worry about
				 * inserting special commands into
				 * the input stream. */
	bool	shutUp = false;	/* true if we put a no echo command
				 * into the command file */
	bool	errOff = false;	/* true if we turned error checking
				 * off before printing the command
				 * and need to turn it back on */
	const char *cmdTemplate;/* Template to use when printing the command */
	char	*cmdStart;	/* Start of expanded command */
	LstNode	*cmdNode;	/* Node for replacing the command */

	noSpecials = (noExecute && !(job->node->type & OP_MAKE));

	if (strcmp(cmd, "...") == 0) {
		job->node->type |= OP_SAVE_CMDS;
		if ((job->flags & JOB_IGNDOTS) == 0) {
			job->tailCmds =
			    Lst_Succ(Lst_Member(&job->node->commands, cmd));
			return (1);
		}
		return (0);
	}

#define	DBPRINTF(fmt, arg)			\
	DEBUGF(JOB, (fmt, arg));		\
	fprintf(job->cmdFILE, fmt, arg);	\
	fflush(job->cmdFILE);

	numCommands += 1;

	/*
	 * For debugging, we replace each command with the result of expanding
	 * the variables in the command.
	 */
	cmdNode = Lst_Member(&job->node->commands, cmd);

	cmd = Buf_Peel(Var_Subst(cmd, job->node, false));
	cmdStart = cmd;

	Lst_Replace(cmdNode, cmdStart);

	cmdTemplate = "%s\n";

	/*
	 * Check for leading @', -' or +'s to control echoing, error checking,
	 * and execution on -n.
	 */
	while (*cmd == '@' || *cmd == '-' || *cmd == '+') {
		switch (*cmd) {

		  case '@':
			shutUp = DEBUG(LOUD) ? false : true;
			break;

		  case '-':
			errOff = true;
			break;

		  case '+':
			if (noSpecials) {
				/*
				 * We're not actually executing anything...
				 * but this one needs to be - use compat mode
				 * just for it.
				 */
				Compat_RunCommand(job->node, cmd, NULL);
				return (0);
			}
			break;
		}
		cmd++;
	}

	while (isspace((unsigned char)*cmd))
		cmd++;

	if (shutUp) {
		if (!(job->flags & JOB_SILENT) && !noSpecials &&
		    shell->hasEchoCtl) {
			DBPRINTF("%s\n", shell->echoOff);
		} else {
			shutUp = false;
		}
	}

	if (errOff) {
		if (!(job->flags & JOB_IGNERR) && !noSpecials) {
			if (shell->hasErrCtl) {
				/*
				 * We don't want the error-control commands
				 * showing up either, so we turn off echoing
				 * while executing them. We could put another
				 * field in the shell structure to tell
				 * JobDoOutput to look for this string too,
				 * but why make it any more complex than
				 * it already is?
				 */
				if (!(job->flags & JOB_SILENT) && !shutUp &&
				    shell->hasEchoCtl) {
					DBPRINTF("%s\n", shell->echoOff);
					DBPRINTF("%s\n", shell->ignErr);
					DBPRINTF("%s\n", shell->echoOn);
				} else {
					DBPRINTF("%s\n", shell->ignErr);
				}
			} else if (shell->ignErr &&
			    *shell->ignErr != '\0') {
				/*
				 * The shell has no error control, so we need to
				 * be weird to get it to ignore any errors from
				 * the command. If echoing is turned on, we turn
				 * it off and use the errCheck template to echo
				 * the command. Leave echoing off so the user
				 * doesn't see the weirdness we go through to
				 * ignore errors. Set cmdTemplate to use the
				 * weirdness instead of the simple "%s\n"
				 * template.
				 */
				if (!(job->flags & JOB_SILENT) && !shutUp &&
				    shell->hasEchoCtl) {
					DBPRINTF("%s\n", shell->echoOff);
					DBPRINTF(shell->errCheck, cmd);
					shutUp = true;
				}
				cmdTemplate = shell->ignErr;
				/*
				 * The error ignoration (hee hee) is already
				 * taken care of by the ignErr template, so
				 * pretend error checking is still on.
				*/
				errOff = false;
			} else {
				errOff = false;
			}
		} else {
			errOff = false;
		}
	}

	DBPRINTF(cmdTemplate, cmd);

	if (errOff) {
		/*
		 * If echoing is already off, there's no point in issuing the
		 * echoOff command. Otherwise we issue it and pretend it was on
		 * for the whole command...
		 */
		if (!shutUp && !(job->flags & JOB_SILENT) &&
		    shell->hasEchoCtl) {
			DBPRINTF("%s\n", shell->echoOff);
			shutUp = true;
		}
		DBPRINTF("%s\n", shell->errCheck);
	}
	if (shutUp) {
		DBPRINTF("%s\n", shell->echoOn);
	}
	return (0);
}

/**
 * JobClose
 *	Called to close both input and output pipes when a job is finished.
 *
 * Side Effects:
 *	The file descriptors associated with the job are closed.
 */
static void
JobClose(Job *job)
{

	if (usePipes) {
		FD_CLR(job->inPipe, &outputs);
		if (job->outPipe != job->inPipe) {
			close(job->outPipe);
		}
		JobDoOutput(job, true);
		close(job->inPipe);
	} else {
		close(job->outFd);
		JobDoOutput(job, true);
	}
}

/**
 * JobFinish
 *	Do final processing for the given job including updating
 *	parents and starting new jobs as available/necessary. Note
 *	that we pay no attention to the JOB_IGNERR flag here.
 *	This is because when we're called because of a noexecute flag
 *	or something, jstat.w_status is 0 and when called from
 *	Job_CatchChildren, the status is zeroed if it s/b ignored.
 *
 * Side Effects:
 *	Some nodes may be put on the toBeMade queue.
 *	Final commands for the job are placed on postCommands.
 *
 *	If we got an error and are aborting (aborting == ABORT_ERROR) and
 *	the job list is now empty, we are done for the day.
 *	If we recognized an error (errors !=0), we set the aborting flag
 *	to ABORT_ERROR so no more jobs will be started.
 */
static void
JobFinish(Job *job, int *status)
{
	bool	done;
	LstNode	*ln;

	if (WIFEXITED(*status)) {
		int	job_status = WEXITSTATUS(*status);

		JobClose(job);
		/*
		 * Deal with ignored errors in -B mode. We need to
		 * print a message telling of the ignored error as
		 * well as setting status.w_status to 0 so the next
		 * command gets run. To do this, we set done to be
		 * true if in -B mode and the job exited non-zero.
		 */
		if (job_status == 0) {
			done = false;
		} else {
			if (job->flags & JOB_IGNERR) {
				done = true;
			} else {
				/*
				 * If it exited non-zero and either we're
				 * doing things our way or we're not ignoring
				 * errors, the job is finished. Similarly, if
				 * the shell died because of a signal the job
				 * is also finished. In these cases, finish
				 * out the job's output before printing the
				 * exit status...
				 */
				done = true;
				if (job->cmdFILE != NULL &&
				    job->cmdFILE != stdout) {
					fclose(job->cmdFILE);
				}

			}
		}
	} else if (WIFSIGNALED(*status)) {
		if (WTERMSIG(*status) == SIGCONT) {
			/*
			 * No need to close things down or anything.
			 */
			done = false;
		} else {
			/*
			 * If it exited non-zero and either we're
			 * doing things our way or we're not ignoring
			 * errors, the job is finished. Similarly, if
			 * the shell died because of a signal the job
			 * is also finished. In these cases, finish
			 * out the job's output before printing the
			 * exit status...
			 */
			JobClose(job);
			if (job->cmdFILE != NULL &&
			    job->cmdFILE != stdout) {
				fclose(job->cmdFILE);
			}
			done = true;
		}
	} else {
		/*
		 * No need to close things down or anything.
		 */
		done = false;
	}

	if (WIFEXITED(*status)) {
		if (done || DEBUG(JOB)) {
			FILE   *out;

			if (compatMake &&
			    !usePipes &&
			    (job->flags & JOB_IGNERR)) {
				/*
				 * If output is going to a file and this job
				 * is ignoring errors, arrange to have the
				 * exit status sent to the output file as
				 * well.
				 */
				out = fdopen(job->outFd, "w");
				if (out == NULL)
					Punt("Cannot fdopen");
			} else {
				out = stdout;
			}

			DEBUGF(JOB, ("Process %jd exited.\n",
			    (intmax_t)job->pid));

			if (WEXITSTATUS(*status) == 0) {
				if (DEBUG(JOB)) {
					if (usePipes && job->node != lastNode) {
						MESSAGE(out, job->node);
						lastNode = job->node;
					}
					fprintf(out,
					    "*** Completed successfully\n");
				}
			} else {
				if (usePipes && job->node != lastNode) {
					MESSAGE(out, job->node);
					lastNode = job->node;
				}
				fprintf(out, "*** Error code %d%s\n",
					WEXITSTATUS(*status),
					(job->flags & JOB_IGNERR) ?
					"(ignored)" : "");

				if (job->flags & JOB_IGNERR) {
					*status = 0;
				}
			}

			fflush(out);
		}
	} else if (WIFSIGNALED(*status)) {
		if (done || DEBUG(JOB) || (WTERMSIG(*status) == SIGCONT)) {
			FILE   *out;

			if (compatMake &&
			    !usePipes &&
			    (job->flags & JOB_IGNERR)) {
				/*
				 * If output is going to a file and this job
				 * is ignoring errors, arrange to have the
				 * exit status sent to the output file as
				 * well.
				 */
				out = fdopen(job->outFd, "w");
				if (out == NULL)
					Punt("Cannot fdopen");
			} else {
				out = stdout;
			}

			if (WTERMSIG(*status) == SIGCONT) {
				/*
				 * If the beastie has continued, shift the
				 * Job from the stopped list to the running
				 * one (or re-stop it if concurrency is
				 * exceeded) and go and get another child.
				 */
				if (job->flags & (JOB_RESUME | JOB_RESTART)) {
					if (usePipes && job->node != lastNode) {
						MESSAGE(out, job->node);
						lastNode = job->node;
					}
					fprintf(out, "*** Continued\n");
				}
				if (!(job->flags & JOB_CONTINUING)) {
					DEBUGF(JOB, ("Warning: process %jd was not "
						     "continuing.\n", (intmax_t) job->pid));
#ifdef notdef
					/*
					 * We don't really want to restart a
					 * job from scratch just because it
					 * continued, especially not without
					 * killing the continuing process!
					 * That's why this is ifdef'ed out.
					 * FD - 9/17/90
					 */
					JobRestart(job);
#endif
				}
				job->flags &= ~JOB_CONTINUING;
				TAILQ_INSERT_TAIL(&jobs, job, link);
				nJobs += 1;
				DEBUGF(JOB, ("Process %jd is continuing locally.\n",
					     (intmax_t) job->pid));
				if (nJobs == maxJobs) {
					jobFull = true;
					DEBUGF(JOB, ("Job queue is full.\n"));
				}
				fflush(out);
				return;

			} else {
				if (usePipes && job->node != lastNode) {
					MESSAGE(out, job->node);
					lastNode = job->node;
				}
				fprintf(out,
				    "*** Signal %d\n", WTERMSIG(*status));
				fflush(out);
			}
		}
	} else {
		/* STOPPED */
		FILE   *out;

		if (compatMake && !usePipes && (job->flags & JOB_IGNERR)) {
			/*
			 * If output is going to a file and this job
			 * is ignoring errors, arrange to have the
			 * exit status sent to the output file as
			 * well.
			 */
			out = fdopen(job->outFd, "w");
			if (out == NULL)
				Punt("Cannot fdopen");
		} else {
			out = stdout;
		}

		DEBUGF(JOB, ("Process %jd stopped.\n", (intmax_t) job->pid));
		if (usePipes && job->node != lastNode) {
			MESSAGE(out, job->node);
			lastNode = job->node;
		}
		fprintf(out, "*** Stopped -- signal %d\n", WSTOPSIG(*status));
		job->flags |= JOB_RESUME;
		TAILQ_INSERT_TAIL(&stoppedJobs, job, link);
		fflush(out);
		return;
	}

	/*
	 * Now handle the -B-mode stuff. If the beast still isn't finished,
	 * try and restart the job on the next command. If JobStart says it's
	 * ok, it's ok. If there's an error, this puppy is done.
	 */
	if (compatMake && WIFEXITED(*status) &&
	    Lst_Succ(job->node->compat_command) != NULL) {
		switch (JobStart(job->node, job->flags & JOB_IGNDOTS, job)) {
		  case JOB_RUNNING:
			done = false;
			break;
		  case JOB_ERROR:
			done = true;
			W_SETEXITSTATUS(status, 1);
			break;
		  case JOB_FINISHED:
			/*
			 * If we got back a JOB_FINISHED code, JobStart has
			 * already called Make_Update and freed the job
			 * descriptor. We set done to false here to avoid fake
			 * cycles and double frees. JobStart needs to do the
			 * update so we can proceed up the graph when given
			 * the -n flag..
			 */
			done = false;
			break;
		  default:
			break;
		}
	} else {
		done = true;
	}

	if (done && aborting != ABORT_ERROR &&
	    aborting != ABORT_INTERRUPT && *status == 0) {
		/*
		 * As long as we aren't aborting and the job didn't return a
		 * non-zero status that we shouldn't ignore, we call
		 * Make_Update to update the parents. In addition, any saved
		 * commands for the node are placed on the .END target.
		 */
		for (ln = job->tailCmds; ln != NULL; ln = LST_NEXT(ln)) {
			Lst_AtEnd(&postCommands->commands,
			    Buf_Peel(
				Var_Subst(Lst_Datum(ln), job->node, false)));
		}

		job->node->made = MADE;
		Make_Update(job->node);
		free(job);

	} else if (*status != 0) {
		errors += 1;
		free(job);
	}

	JobRestartJobs();

	/*
	 * Set aborting if any error.
	 */
	if (errors && !keepgoing && aborting != ABORT_INTERRUPT) {
		/*
		 * If we found any errors in this batch of children and the -k
		 * flag wasn't given, we set the aborting flag so no more jobs
		 * get started.
		 */
		aborting = ABORT_ERROR;
	}

	if (aborting == ABORT_ERROR && Job_Empty()) {
		/*
		 * If we are aborting and the job table is now empty, we finish.
		 */
		Finish(errors);
	}
}

/**
 * JobTouch
 *	Touch the given target. Called by JobStart when the -t flag was
 *	given.  Prints messages unless told to be silent.
 *
 * Side Effects:
 *	The data modification of the file is changed. In addition, if the
 *	file did not exist, it is created.
 */
static void
JobTouch(GNode *gn, bool silent)
{
	int	streamID;	/* ID of stream opened to do the touch */
	struct utimbuf times;	/* Times for utime() call */

	if (gn->type & (OP_JOIN | OP_USE | OP_EXEC | OP_OPTIONAL)) {
		/*
		 * .JOIN, .USE, .ZEROTIME and .OPTIONAL targets are "virtual"
		 * targets and, as such, shouldn't really be created.
		 */
		return;
	}

	if (!silent) {
		fprintf(stdout, "touch %s\n", gn->name);
		fflush(stdout);
	}

	if (noExecute) {
		return;
	}

	if (gn->type & OP_ARCHV) {
		Arch_Touch(gn);
	} else if (gn->type & OP_LIB) {
		Arch_TouchLib(gn);
	} else {
		char	*file = gn->path ? gn->path : gn->name;

		times.actime = times.modtime = now;
		if (utime(file, &times) < 0) {
			streamID = open(file, O_RDWR | O_CREAT, 0666);

			if (streamID >= 0) {
				char	c;

				/*
				 * Read and write a byte to the file to change
				 * the modification time, then close the file.
				 */
				if (read(streamID, &c, 1) == 1) {
					lseek(streamID, (off_t)0, SEEK_SET);
					write(streamID, &c, 1);
				}

				close(streamID);
			} else {
				fprintf(stdout, "*** couldn't touch %s: %s",
				    file, strerror(errno));
				fflush(stdout);
			}
		}
	}
}

/**
 * JobCheckCommands
 *	Make sure the given node has all the commands it needs.
 *
 * Results:
 *	true if the commands list is/was ok.
 *
 * Side Effects:
 *	The node will have commands from the .DEFAULT rule added to it
 *	if it needs them.
 */
bool
JobCheckCommands(GNode *gn, AbortProc *abortProc)
{
	const char *msg = "make: don't know how to make";

	if (!OP_NOP(gn->type)) {
		return (true);		/* this node does nothing */
	}

	if (!Lst_IsEmpty(&gn->commands)) {
		return (true);		/* this node has no commands */
	}

	if (gn->type & OP_LIB) {
		return (true);
	}

	/*
	 * No commands. Look for .DEFAULT rule from which we might infer
	 * commands.
	 */
	if (DEFAULT != NULL && !Lst_IsEmpty(&DEFAULT->commands)) {
		/*
		 * Make only looks for a .DEFAULT if the node was
		 * never the target of an operator, so that's what we
		 * do too. If a .DEFAULT was given, we substitute its
		 * commands for gn's commands and set the IMPSRC
		 * variable to be the target's name The DEFAULT node
		 * acts like a transformation rule, in that gn also
		 * inherits any attributes or sources attached to
		 * .DEFAULT itself.
		 */
		Make_HandleUse(DEFAULT, gn);
		Var_Set(IMPSRC, Var_Value(TARGET, gn), gn);
		return (true);
	}

	if (Dir_MTime(gn) != 0) {
		return (true);
	}

	/*
	 * The node wasn't the target of an operator we have
	 * no .DEFAULT rule to go on and the target doesn't
	 * already exist. There's nothing more we can do for
	 * this branch. If the -k flag wasn't given, we stop
	 * in our tracks, otherwise we just don't update
	 * this node's parents so they never get examined.
	 */
	if (gn->type & OP_OPTIONAL) {
		fprintf(stdout, "%s %s(ignored)\n", msg, gn->name);
		fflush(stdout);
		return (true);
	}

	if (keepgoing) {
		fprintf(stdout, "%s %s(continuing)\n", msg, gn->name);
		fflush(stdout);
		return (false);
	}

#if OLD_JOKE
	if (strcmp(gn->name,"love") == 0)
		abortProc("Not war.");
	else
#endif
		abortProc("%s %s. Stop", msg, gn->name);

	return (false);
}

/**
 * JobExec
 *	Execute the shell for the given job. Called from JobStart and
 *	JobRestart.
 *
 * Side Effects:
 *	A shell is executed, outputs is altered and the Job structure added
 *	to the job table.
 */
static void
JobExec(Job *job, char **argv)
{
	struct Shell	*shell = job->shell;
	ProcStuff	ps;

	if (DEBUG(JOB)) {
		int	  i;

		DEBUGF(JOB, ("Running %s\n", job->node->name));
		DEBUGF(JOB, ("\tCommand: "));
		for (i = 0; argv[i] != NULL; i++) {
			DEBUGF(JOB, ("%s ", argv[i]));
		}
		DEBUGF(JOB, ("\n"));
	}

	/*
	 * Some jobs produce no output and it's disconcerting to have
	 * no feedback of their running (since they produce no output, the
	 * banner with their name in it never appears). This is an attempt to
	 * provide that feedback, even if nothing follows it.
	 */
	if (lastNode != job->node && (job->flags & JOB_FIRST) &&
	    !(job->flags & JOB_SILENT)) {
		MESSAGE(stdout, job->node);
		lastNode = job->node;
	}

	ps.in = FILENO(job->cmdFILE);
	if (usePipes) {
		/*
		 * Set up the child's output to be routed through the
		 * pipe we've created for it.
		 */
		ps.out = job->outPipe;
	} else {
		/*
		 * We're capturing output in a file, so we duplicate
		 * the descriptor to the temporary file into the
		 * standard output.
		 */
		ps.out = job->outFd;
	}
	ps.err = STDERR_FILENO;

	ps.merge_errors = 1;
	ps.pgroup = 1;
	ps.searchpath = 0;

	ps.argv = argv;
	ps.argv_free = 0;

	/*
	 * Fork.  Warning since we are doing vfork() instead of fork(),
	 * do not allocate memory in the child process!
	 */
	if ((ps.child_pid = vfork()) == -1) {
		Punt("Cannot fork");

	} else if (ps.child_pid == 0) {
		/*
		 * Child
		 */
		if (fifoFd >= 0)
			close(fifoFd);

		Proc_Exec(&ps, shell);
		/* NOTREACHED */

	} else {
		/*
		 * Parent
		 */
		job->pid = ps.child_pid;

		if (usePipes && (job->flags & JOB_FIRST)) {
			/*
			 * The first time a job is run for a node, we set the
			 * current position in the buffer to the beginning and
			 * mark another stream to watch in the outputs mask.
			 */
			job->curPos = 0;
			FD_SET(job->inPipe, &outputs);
		}

		if (job->cmdFILE != NULL && job->cmdFILE != stdout) {
			fclose(job->cmdFILE);
			job->cmdFILE = NULL;
		}

		/*
		 * Now the job is actually running, add it to the table.
		 */
		nJobs += 1;
		TAILQ_INSERT_TAIL(&jobs, job, link);
		if (nJobs == maxJobs) {
			jobFull = true;
		}
	}
}

/**
 * JobMakeArgv
 *	Create the argv needed to execute the shell for a given job.
 */
static void
JobMakeArgv(Job *job, char **argv)
{
	struct Shell	*shell = job->shell;
	int		argc;
	static char	args[10];	/* For merged arguments */

	argv[0] = shell->name;
	argc = 1;

	if ((shell->exit && *shell->exit != '-') ||
	    (shell->echo && *shell->echo != '-')) {
		/*
		 * At least one of the flags doesn't have a minus before it, so
		 * merge them together. Have to do this because the *(&(@*#*&#$#
		 * Bourne shell thinks its second argument is a file to source.
		 * Grrrr. Note the ten-character limitation on the combined
		 * arguments.
		 */
		sprintf(args, "-%s%s", (job->flags & JOB_IGNERR) ? "" :
		    shell->exit ? shell->exit : "",
		    (job->flags & JOB_SILENT) ? "" :
		    shell->echo ? shell->echo : "");

		if (args[1]) {
			argv[argc] = args;
			argc++;
		}
	} else {
		if (!(job->flags & JOB_IGNERR) && shell->exit) {
			argv[argc] = shell->exit;
			argc++;
		}
		if (!(job->flags & JOB_SILENT) && shell->echo) {
			argv[argc] = shell->echo;
			argc++;
		}
	}
	argv[argc] = NULL;
}

/**
 * JobRestart
 *	Restart a job that stopped for some reason. The job must be neither
 *	on the jobs nor on the stoppedJobs list.
 *
 * Side Effects:
 *	jobFull will be set if the job couldn't be run.
 */
static void
JobRestart(Job *job)
{

	if (job->flags & JOB_RESTART) {
		/*
		 * Set up the control arguments to the shell. This is based on
		 * the flags set earlier for this job. If the JOB_IGNERR flag
		 * is clear, the 'exit' flag of the shell is used to
		 * cause it to exit upon receiving an error. If the JOB_SILENT
		 * flag is clear, the 'echo' flag of the shell is used
		 * to get it to start echoing as soon as it starts
		 * processing commands.
		 */
		char	*argv[4];

		JobMakeArgv(job, argv);

		DEBUGF(JOB, ("Restarting %s...", job->node->name));
		if (nJobs >= maxJobs && !(job->flags & JOB_SPECIAL)) {
			/*
			 * Not allowed to run -- put it back on the hold
			 * queue and mark the table full
			 */
			DEBUGF(JOB, ("holding\n"));
			TAILQ_INSERT_HEAD(&stoppedJobs, job, link);
			jobFull = true;
			DEBUGF(JOB, ("Job queue is full.\n"));
			return;
		} else {
			/*
			 * Job may be run locally.
			 */
			DEBUGF(JOB, ("running locally\n"));
		}
		JobExec(job, argv);

	} else {
		/*
		 * The job has stopped and needs to be restarted.
		 * Why it stopped, we don't know...
		 */
		DEBUGF(JOB, ("Resuming %s...", job->node->name));
		if ((nJobs < maxJobs || ((job->flags & JOB_SPECIAL) &&
		    maxJobs == 0)) && nJobs != maxJobs) {
			/*
			 * If we haven't reached the concurrency limit already
			 * (or the job must be run and maxJobs is 0), it's ok
			 * to resume it.
			 */
			bool error;
			int status;

			error = (KILL(job->pid, SIGCONT) != 0);

			if (!error) {
				/*
				 * Make sure the user knows we've continued
				 * the beast and actually put the thing in the
				 * job table.
				 */
				job->flags |= JOB_CONTINUING;
				status = 0;
				W_SETTERMSIG(&status, SIGCONT);
				JobFinish(job, &status);

				job->flags &= ~(JOB_RESUME|JOB_CONTINUING);
				DEBUGF(JOB, ("done\n"));
			} else {
				Error("couldn't resume %s: %s",
				job->node->name, strerror(errno));
				status = 0;
				W_SETEXITSTATUS(&status, 1);
				JobFinish(job, &status);
			}
		} else {
			/*
			* Job cannot be restarted. Mark the table as full and
			* place the job back on the list of stopped jobs.
			*/
			DEBUGF(JOB, ("table full\n"));
			TAILQ_INSERT_HEAD(&stoppedJobs, job, link);
			jobFull = true;
			DEBUGF(JOB, ("Job queue is full.\n"));
		}
	}
}

/**
 * JobStart
 *	Start a target-creation process going for the target described
 *	by the graph node gn.
 *
 * Results:
 *	JOB_ERROR if there was an error in the commands, JOB_FINISHED
 *	if there isn't actually anything left to do for the job and
 *	JOB_RUNNING if the job has been started.
 *
 * Side Effects:
 *	A new Job node is created and added to the list of running
 *	jobs. PMake is forked and a child shell created.
 */
static int
JobStart(GNode *gn, int flags, Job *previous)
{
	Job	*job;		/* new job descriptor */
	char	*argv[4];	/* Argument vector to shell */
	bool	cmdsOK;		/* true if the nodes commands were all right */
	bool	noExec;		/* Set true if we decide not to run the job */
	int	tfd;		/* File descriptor for temp file */
	LstNode	*ln;
	char	tfile[sizeof(TMPPAT)];

	if (previous == NULL) {
		job = emalloc(sizeof(Job));
		flags |= JOB_FIRST;
	} else {
		previous->flags &= ~(JOB_FIRST | JOB_IGNERR | JOB_SILENT);
		job = previous;
	}

	job->shell = commandShell;
	job->node = gn;
	job->tailCmds = NULL;

	/*
	 * Set the initial value of the flags for this job based on the global
	 * ones and the node's attributes... Any flags supplied by the caller
	 * are also added to the field.
	 */
	job->flags = 0;
	if (Targ_Ignore(gn)) {
		job->flags |= JOB_IGNERR;
	}
	if (Targ_Silent(gn)) {
		job->flags |= JOB_SILENT;
	}
	job->flags |= flags;

	/*
	 * Check the commands now so any attributes from .DEFAULT have a chance
	 * to migrate to the node.
	 */
	if (!compatMake && (job->flags & JOB_FIRST)) {
		cmdsOK = JobCheckCommands(gn, Error);
	} else {
		cmdsOK = true;
	}

	/*
	 * If the -n flag wasn't given, we open up OUR (not the child's)
	 * temporary file to stuff commands in it. The thing is rd/wr so we
	 * don't need to reopen it to feed it to the shell. If the -n flag
	 * *was* given, we just set the file to be stdout. Cute, huh?
	 */
	if ((gn->type & OP_MAKE) || (!noExecute && !touchFlag)) {
		/*
		 * We're serious here, but if the commands were bogus, we're
		 * also dead...
		 */
		if (!cmdsOK) {
			DieHorribly();
		}

		strcpy(tfile, TMPPAT);
		if ((tfd = mkstemp(tfile)) == -1)
			Punt("Cannot create temp file: %s", strerror(errno));
		job->cmdFILE = fdopen(tfd, "w+");
		eunlink(tfile);
		if (job->cmdFILE == NULL) {
			close(tfd);
			Punt("Could not open %s", tfile);
		}
		fcntl(FILENO(job->cmdFILE), F_SETFD, 1);
		/*
		 * Send the commands to the command file, flush all its
		 * buffers then rewind and remove the thing.
		 */
		noExec = false;

		/*
		 * Used to be backwards; replace when start doing multiple
		 * commands per shell.
		 */
		if (compatMake) {
			/*
			 * Be compatible: If this is the first time for this
			 * node, verify its commands are ok and open the
			 * commands list for sequential access by later
			 * invocations of JobStart. Once that is done, we take
			 * the next command off the list and print it to the
			 * command file. If the command was an ellipsis, note
			 * that there's nothing more to execute.
			 */
			if (job->flags & JOB_FIRST)
				gn->compat_command = Lst_First(&gn->commands);
			else
				gn->compat_command =
				    Lst_Succ(gn->compat_command);

			if (gn->compat_command == NULL ||
			    JobPrintCommand(Lst_Datum(gn->compat_command), job))
				noExec = true;

			if (noExec && !(job->flags & JOB_FIRST)) {
				/*
				 * If we're not going to execute anything, the
				 * job is done and we need to close down the
				 * various file descriptors we've opened for
				 * output, then call JobDoOutput to catch the
				 * final characters or send the file to the
				 * screen... Note that the i/o streams are only
				 * open if this isn't the first job. Note also
				 * that this could not be done in
				 * Job_CatchChildren b/c it wasn't clear if
				 * there were more commands to execute or not...
				 */
				JobClose(job);
			}
		} else {
			/*
			 * We can do all the commands at once. hooray for sanity
			 */
			numCommands = 0;
			LST_FOREACH(ln, &gn->commands) {
				if (JobPrintCommand(Lst_Datum(ln), job))
					break;
			}

			/*
			 * If we didn't print out any commands to the shell
			 * script, there's not much point in executing the
			 * shell, is there?
			 */
			if (numCommands == 0) {
				noExec = true;
			}
		}

	} else if (noExecute) {
		/*
		 * Not executing anything -- just print all the commands to
		 * stdout in one fell swoop. This will still set up
		 * job->tailCmds correctly.
		 */
		if (lastNode != gn) {
			MESSAGE(stdout, gn);
			lastNode = gn;
		}
		job->cmdFILE = stdout;

		/*
		 * Only print the commands if they're ok, but don't die if
		 * they're not -- just let the user know they're bad and keep
		 * going. It doesn't do any harm in this case and may do
		 * some good.
		 */
		if (cmdsOK) {
			LST_FOREACH(ln, &gn->commands) {
				if (JobPrintCommand(Lst_Datum(ln), job))
					break;
			}
		}
		/*
		* Don't execute the shell, thank you.
		*/
		noExec = true;

	} else {
		/*
		 * Just touch the target and note that no shell should be
		 * executed. Set cmdFILE to stdout to make life easier. Check
		 * the commands, too, but don't die if they're no good -- it
		 * does no harm to keep working up the graph.
		 */
		job->cmdFILE = stdout;
		JobTouch(gn, job->flags & JOB_SILENT);
		noExec = true;
	}

	/*
	 * If we're not supposed to execute a shell, don't.
	 */
	if (noExec) {
		/*
		 * Unlink and close the command file if we opened one
		 */
		if (job->cmdFILE != stdout) {
			if (job->cmdFILE != NULL)
				fclose(job->cmdFILE);
		} else {
			fflush(stdout);
		}

		/*
		 * We only want to work our way up the graph if we aren't here
		 * because the commands for the job were no good.
		*/
		if (cmdsOK) {
			if (aborting == 0) {
				for (ln = job->tailCmds; ln != NULL;
				    ln = LST_NEXT(ln)) {
					Lst_AtEnd(&postCommands->commands,
					    Buf_Peel(Var_Subst(Lst_Datum(ln),
					    job->node, false)));
				}
				job->node->made = MADE;
				Make_Update(job->node);
			}
			free(job);
			return(JOB_FINISHED);
		} else {
			free(job);
			return(JOB_ERROR);
		}
	} else {
		fflush(job->cmdFILE);
	}

	/*
	 * Set up the control arguments to the shell. This is based on the flags
	 * set earlier for this job.
	 */
	JobMakeArgv(job, argv);

	/*
	 * If we're using pipes to catch output, create the pipe by which we'll
	 * get the shell's output. If we're using files, print out that we're
	 * starting a job and then set up its temporary-file name.
	 */
	if (!compatMake || (job->flags & JOB_FIRST)) {
		if (usePipes) {
			int fd[2];

			if (pipe(fd) == -1)
				Punt("Cannot create pipe: %s", strerror(errno));
			job->inPipe = fd[0];
			job->outPipe = fd[1];
			fcntl(job->inPipe, F_SETFD, 1);
			fcntl(job->outPipe, F_SETFD, 1);
		} else {
			fprintf(stdout, "Remaking `%s'\n", gn->name);
			fflush(stdout);
			strcpy(job->outFile, TMPPAT);
			if ((job->outFd = mkstemp(job->outFile)) == -1)
				Punt("cannot create temp file: %s",
				    strerror(errno));
			fcntl(job->outFd, F_SETFD, 1);
		}
	}

	if (nJobs >= maxJobs && !(job->flags & JOB_SPECIAL) && maxJobs != 0) {
		/*
		 * We've hit the limit of concurrency, so put the job on hold
		 * until some other job finishes. Note that the special jobs
		 * (.BEGIN, .INTERRUPT and .END) may be run even when the
		 * limit has been reached (e.g. when maxJobs == 0).
		 */
		jobFull = true;

		DEBUGF(JOB, ("Can only run job locally.\n"));
		job->flags |= JOB_RESTART;
		TAILQ_INSERT_TAIL(&stoppedJobs, job, link);
	} else {
		if (nJobs >= maxJobs) {
			/*
			 * If we're running this job as a special case
			 * (see above), at least say the table is full.
			 */
			jobFull = true;
			DEBUGF(JOB, ("Local job queue is full.\n"));
		}
		JobExec(job, argv);
	}
	return (JOB_RUNNING);
}

static char *
JobOutput(Job *job, char *cp, char *endp, int msg)
{
	struct Shell	*shell = job->shell;
	char		*ecp;

	if (shell->noPrint) {
		ecp = strstr(cp, shell->noPrint);
		while (ecp != NULL) {
			if (cp != ecp) {
				*ecp = '\0';
				if (msg && job->node != lastNode) {
					MESSAGE(stdout, job->node);
					lastNode = job->node;
				}
				/*
				 * The only way there wouldn't be a newline
				 * after this line is if it were the last in
				 * the buffer. However, since the non-printable
				 * comes after it, there must be a newline, so
				 * we don't print one.
				 */
				fprintf(stdout, "%s", cp);
				fflush(stdout);
			}
			cp = ecp + strlen(shell->noPrint);
			if (cp != endp) {
				/*
				 * Still more to print, look again after
				 * skipping the whitespace following the
				 * non-printable command....
				 */
				cp++;
				while (*cp == ' ' || *cp == '\t' ||
				    *cp == '\n') {
					cp++;
				}
				ecp = strstr(cp, shell->noPrint);
			} else {
				return (cp);
			}
		}
	}
	return (cp);
}

/**
 * JobDoOutput
 *	This function is called at different times depending on
 *	whether the user has specified that output is to be collected
 *	via pipes or temporary files. In the former case, we are called
 *	whenever there is something to read on the pipe. We collect more
 *	output from the given job and store it in the job's outBuf. If
 *	this makes up a line, we print it tagged by the job's identifier,
 *	as necessary.
 *	If output has been collected in a temporary file, we open the
 *	file and read it line by line, transferring it to our own
 *	output channel until the file is empty. At which point we
 *	remove the temporary file.
 *	In both cases, however, we keep our figurative eye out for the
 *	'noPrint' line for the shell from which the output came. If
 *	we recognize a line, we don't print it. If the command is not
 *	alone on the line (the character after it is not \0 or \n), we
 *	do print whatever follows it.
 *
 * Side Effects:
 *	curPos may be shifted as may the contents of outBuf.
 */
static void
JobDoOutput(Job *job, bool finish)
{
	bool	gotNL = false;	/* true if got a newline */
	bool	fbuf;		/* true if our buffer filled up */
	int	nr;		/* number of bytes read */
	int	i;		/* auxiliary index into outBuf */
	int	max;		/* limit for i (end of current data) */
	int	nRead;		/* (Temporary) number of bytes read */
	FILE	*oFILE;		/* Stream pointer to shell's output file */
	char	inLine[132];

	if (usePipes) {
		/*
		 * Read as many bytes as will fit in the buffer.
		 */
  end_loop:
		gotNL = false;
		fbuf = false;

		nRead = read(job->inPipe, &job->outBuf[job->curPos],
		    JOB_BUFSIZE - job->curPos);
		/*
		 * Check for interrupt here too, because the above read may
		 * block when the child process is stopped. In this case the
		 * interrupt will unblock it (we don't use SA_RESTART).
		 */
		SigHandler();

		if (nRead < 0) {
			DEBUGF(JOB, ("JobDoOutput(piperead)"));
			nr = 0;
		} else {
			nr = nRead;
		}

		/*
		 * If we hit the end-of-file (the job is dead), we must flush
		 * its remaining output, so pretend we read a newline if
		 * there's any output remaining in the buffer.
		 * Also clear the 'finish' flag so we stop looping.
		 */
		if (nr == 0 && job->curPos != 0) {
			job->outBuf[job->curPos] = '\n';
			nr = 1;
			finish = false;
		} else if (nr == 0) {
			finish = false;
		}

		/*
		 * Look for the last newline in the bytes we just got. If there
		 * is one, break out of the loop with 'i' as its index and
		 * gotNL set true.
		*/
		max = job->curPos + nr;
		for (i = job->curPos + nr - 1; i >= job->curPos; i--) {
			if (job->outBuf[i] == '\n') {
				gotNL = true;
				break;
			} else if (job->outBuf[i] == '\0') {
				/*
				 * Why?
				 */
				job->outBuf[i] = ' ';
			}
		}

		if (!gotNL) {
			job->curPos += nr;
			if (job->curPos == JOB_BUFSIZE) {
				/*
				 * If we've run out of buffer space, we have
				 * no choice but to print the stuff. sigh.
				 */
				fbuf = true;
				i = job->curPos;
			}
		}
		if (gotNL || fbuf) {
			/*
			 * Need to send the output to the screen. Null terminate
			 * it first, overwriting the newline character if there
			 * was one. So long as the line isn't one we should
			 * filter (according to the shell description), we print
			 * the line, preceded by a target banner if this target
			 * isn't the same as the one for which we last printed
			 * something. The rest of the data in the buffer are
			 * then shifted down to the start of the buffer and
			 * curPos is set accordingly.
			 */
			job->outBuf[i] = '\0';
			if (i >= job->curPos) {
				char *cp;

				cp = JobOutput(job, job->outBuf,
				    &job->outBuf[i], false);

				/*
				 * There's still more in that buffer. This time,
				 * though, we know there's no newline at the
				 * end, so we add one of our own free will.
				 */
				if (*cp != '\0') {
					if (job->node != lastNode) {
						MESSAGE(stdout, job->node);
						lastNode = job->node;
					}
					fprintf(stdout, "%s%s", cp,
					    gotNL ? "\n" : "");
					fflush(stdout);
				}
			}
			if (i < max - 1) {
				/* shift the remaining characters down */
				memcpy(job->outBuf, &job->outBuf[i + 1],
				    max - (i + 1));
				job->curPos = max - (i + 1);

			} else {
				/*
				 * We have written everything out, so we just
				 * start over from the start of the buffer.
				 * No copying. No nothing.
				 */
				job->curPos = 0;
			}
		}
		if (finish) {
			/*
			 * If the finish flag is true, we must loop until we hit
			 * end-of-file on the pipe. This is guaranteed to happen
			 * eventually since the other end of the pipe is now
			 * closed (we closed it explicitly and the child has
			 * exited). When we do get an EOF, finish will be set
			 * false and we'll fall through and out.
			 */
			goto end_loop;
		}

	} else {
		/*
		 * We've been called to retrieve the output of the job from the
		 * temporary file where it's been squirreled away. This consists
		 * of opening the file, reading the output line by line, being
		 * sure not to print the noPrint line for the shell we used,
		 * then close and remove the temporary file. Very simple.
		 *
		 * Change to read in blocks and do FindSubString type things
		 * as for pipes? That would allow for "@echo -n..."
		 */
		oFILE = fopen(job->outFile, "r");
		if (oFILE != NULL) {
			fprintf(stdout, "Results of making %s:\n",
			    job->node->name);
			fflush(stdout);

			while (fgets(inLine, sizeof(inLine), oFILE) != NULL) {
				char	*cp, *endp, *oendp;

				cp = inLine;
				oendp = endp = inLine + strlen(inLine);
				if (endp[-1] == '\n') {
					*--endp = '\0';
				}
				cp = JobOutput(job, inLine, endp, false);

				/*
				 * There's still more in that buffer. This time,
				 * though, we know there's no newline at the
				 * end, so we add one of our own free will.
				 */
				fprintf(stdout, "%s", cp);
				fflush(stdout);
				if (endp != oendp) {
					fprintf(stdout, "\n");
					fflush(stdout);
				}
			}
			fclose(oFILE);
			eunlink(job->outFile);
		}
	}
}

/**
 * Job_CatchChildren
 *	Handle the exit of a child. Called from Make_Make.
 *
 * Side Effects:
 *	The job descriptor is removed from the list of children.
 *
 * Notes:
 *	We do waits, blocking or not, according to the wisdom of our
 *	caller, until there are no more children to report. For each
 *	job, call JobFinish to finish things off. This will take care of
 *	putting jobs on the stoppedJobs queue.
 */
void
Job_CatchChildren(bool block)
{
	pid_t	pid;	/* pid of dead child */
	Job	*job;	/* job descriptor for dead child */
	int	status;	/* Exit/termination status */

	/*
	 * Don't even bother if we know there's no one around.
	 */
	if (nJobs == 0) {
		return;
	}

	for (;;) {
		pid = waitpid((pid_t)-1, &status, (block ? 0 : WNOHANG));
		if (pid <= 0)
			break;

		DEBUGF(JOB, ("Process %jd exited or stopped.\n",
		    (intmax_t)pid));

		TAILQ_FOREACH(job, &jobs, link) {
			if (job->pid == pid)
				break;
		}

		if (job == NULL) {
			if (WIFSIGNALED(status) &&
			    (WTERMSIG(status) == SIGCONT)) {
				TAILQ_FOREACH(job, &jobs, link) {
					if (job->pid == pid)
						break;
				}
				if (job == NULL) {
					Error("Resumed child (%jd) "
					    "not in table", (intmax_t)pid);
					continue;
				}
				TAILQ_REMOVE(&stoppedJobs, job, link);
			} else {
				Error("Child (%jd) not in table?",
				    (intmax_t)pid);
				continue;
			}
		} else {
			TAILQ_REMOVE(&jobs, job, link);
			nJobs -= 1;
			if (fifoFd >= 0 && maxJobs > 1) {
				write(fifoFd, "+", 1);
				maxJobs--;
				if (nJobs >= maxJobs)
					jobFull = true;
				else
					jobFull = false;
			} else {
				DEBUGF(JOB, ("Job queue is no longer full.\n"));
				jobFull = false;
			}
		}

		JobFinish(job, &status);
	}
	SigHandler();
}

/**
 * Job_CatchOutput
 *	Catch the output from our children, if we're using
 *	pipes do so. Otherwise just block time until we get a
 *	signal(most likely a SIGCHLD) since there's no point in
 *	just spinning when there's nothing to do and the reaping
 *	of a child can wait for a while.
 *
 * Side Effects:
 *	Output is read from pipes if we're piping.
 */
void
Job_CatchOutput(int flag)
{
	int		nfds;
	struct timeval	timeout;
	fd_set		readfds;
	Job		*job;

	fflush(stdout);

	if (usePipes) {
		readfds = outputs;
		timeout.tv_sec = SEL_SEC;
		timeout.tv_usec = SEL_USEC;
		if (flag && jobFull && fifoFd >= 0)
			FD_SET(fifoFd, &readfds);

		nfds = select(FD_SETSIZE, &readfds, NULL, NULL, &timeout);
		if (nfds == 0) {
			/* timeout expired */
			SigHandler();
			return;
		} else if (nfds < 0) {
			/* must be EINTR */
			SigHandler();
			return;
		} else {
			if (fifoFd >= 0 && FD_ISSET(fifoFd, &readfds)) {
				if (--nfds <= 0)
					return;
			}
			job = TAILQ_FIRST(&jobs);
			while (nfds != 0 && job != NULL) {
				if (FD_ISSET(job->inPipe, &readfds)) {
					JobDoOutput(job, false);
					nfds--;
				}
				job = TAILQ_NEXT(job, link);
			}
		}
	}
}

/**
 * Job_Make
 *	Start the creation of a target. Basically a front-end for
 *	JobStart used by the Make module.
 *
 * Side Effects:
 *	Another job is started.
 */
void
Job_Make(GNode *gn)
{

	JobStart(gn, 0, NULL);
}

/**
 * Job_Init
 *	Initialize the process module, given a maximum number of jobs.
 *
 * Side Effects:
 *	lists and counters are initialized
 */
void
Job_Init(int maxproc)
{
	GNode		*begin;	/* node for commands to do at the very start */
	const char	*env;

	fifoFd = -1;
	env = getenv("MAKE_JOBS_FIFO");

	if (env == NULL && maxproc > 1) {
		/*
		 * We did not find the environment variable so we are the
		 * leader. Create the fifo, open it, write one char per
		 * allowed job into the pipe.
		 */
		fifoFd = mkfifotemp(fifoName);
		if (fifoFd < 0) {
			env = NULL;
		} else {
			fifoMaster = 1;
			fcntl(fifoFd, F_SETFL, O_NONBLOCK);
			env = fifoName;
			if (setenv("MAKE_JOBS_FIFO", env, 1) == -1)
				Punt("setenv: MAKE_JOBS_FIFO: can't allocate memory");
			while (maxproc-- > 0) {
				write(fifoFd, "+", 1);
			}
			/* The master make does not get a magic token */
			jobFull = true;
			maxJobs = 0;
		}

	} else if (env != NULL) {
		/*
		 * We had the environment variable so we are a slave.
		 * Open fifo and give ourselves a magic token which represents
		 * the token our parent make has grabbed to start his make
		 * process. Otherwise the sub-makes would gobble up tokens and
		 * the proper number of tokens to specify to -j would depend
		 * on the depth of the tree and the order of execution.
		 */
		fifoFd = open(env, O_RDWR, 0);
		if (fifoFd >= 0) {
			fcntl(fifoFd, F_SETFL, O_NONBLOCK);
			maxJobs = 1;
			jobFull = false;
		}
	}
	if (fifoFd <= 0) {
		maxJobs = maxproc;
		jobFull = false;
	} else {
	}
	nJobs = 0;

	aborting = 0;
	errors = 0;

	lastNode = NULL;

	if ((maxJobs == 1 && fifoFd < 0) || beVerbose == 0) {
		/*
		 * If only one job can run at a time, there's no need for a
		 * banner, no is there?
		 */
		targFmt = "";
	} else {
		targFmt = TARG_FMT;
	}

	begin = Targ_FindNode(".BEGIN", TARG_NOCREATE);

	if (begin != NULL) {
		JobStart(begin, JOB_SPECIAL, NULL);
		while (nJobs) {
			Job_CatchOutput(0);
			Job_CatchChildren(!usePipes);
		}
	}
	postCommands = Targ_FindNode(".END", TARG_CREATE);
}

/**
 * Job_Full
 *	See if the job table is full. It is considered full if it is OR
 *	if we are in the process of aborting OR if we have
 *	reached/exceeded our local quota. This prevents any more jobs
 *	from starting up.
 *
 * Results:
 *	true if the job table is full, false otherwise
 */
bool
Job_Full(void)
{
	char c;
	int i;

	if (aborting)
		return (aborting);
	if (fifoFd >= 0 && jobFull) {
		i = read(fifoFd, &c, 1);
		if (i > 0) {
			maxJobs++;
			jobFull = false;
		}
	}
	return (jobFull);
}

/**
 * Job_Empty
 *	See if the job table is empty.  Because the local concurrency may
 *	be set to 0, it is possible for the job table to become empty,
 *	while the list of stoppedJobs remains non-empty. In such a case,
 *	we want to restart as many jobs as we can.
 *
 * Results:
 *	true if it is. false if it ain't.
 */
bool
Job_Empty(void)
{

	if (nJobs == 0) {
		if (!TAILQ_EMPTY(&stoppedJobs) && !aborting) {
			/*
			 * The job table is obviously not full if it has no
			 * jobs in it...Try and restart the stopped jobs.
			 */
			jobFull = false;
			JobRestartJobs();
			return (false);
		} else {
			return (true);
		}
	} else {
		return (false);
	}
}

/**
 * Job_Finish
 *	Do final processing such as the running of the commands
 *	attached to the .END target.
 *
 * Results:
 *	Number of errors reported.
 */
int
Job_Finish(void)
{

	if (postCommands != NULL && !Lst_IsEmpty(&postCommands->commands)) {
		if (errors) {
			Error("Errors reported so .END ignored");
		} else {
			JobStart(postCommands, JOB_SPECIAL | JOB_IGNDOTS, NULL);

			while (nJobs) {
				Job_CatchOutput(0);
				Job_CatchChildren(!usePipes);
			}
		}
	}
	if (fifoFd >= 0) {
		close(fifoFd);
		fifoFd = -1;
		if (fifoMaster)
			unlink(fifoName);
	}
	return (errors);
}

/**
 * Job_Wait
 *	Waits for all running jobs to finish and returns. Sets 'aborting'
 *	to ABORT_WAIT to prevent other jobs from starting.
 *
 * Side Effects:
 *	Currently running jobs finish.
 */
void
Job_Wait(void)
{

	aborting = ABORT_WAIT;
	while (nJobs != 0) {
		Job_CatchOutput(0);
		Job_CatchChildren(!usePipes);
	}
	aborting = 0;
}

/**
 * Job_AbortAll
 *	Abort all currently running jobs without handling output or anything.
 *	This function is to be called only in the event of a major
 *	error.
 *
 * Side Effects:
 *	All children are killed, not just the firstborn
 */
void
Job_AbortAll(void)
{
	Job	*job;	/* the job descriptor in that element */
	int	status;

	aborting = ABORT_ERROR;

	if (nJobs) {
		TAILQ_FOREACH(job, &jobs, link) {
			/*
			 * kill the child process with increasingly drastic
			 * signals to make darn sure it's dead.
			 */
			KILL(job->pid, SIGINT);
			KILL(job->pid, SIGKILL);
		}
	}

	/*
	 * Catch as many children as want to report in at first, then give up
	 */
	while (waitpid((pid_t)-1, &status, WNOHANG) > 0)
		;
}

/**
 * JobRestartJobs
 *	Tries to restart stopped jobs if there are slots available.
 *	Note that this tries to restart them regardless of pending errors.
 *	It's not good to leave stopped jobs lying around!
 *
 * Side Effects:
 *	Resumes(and possibly migrates) jobs.
 */
static void
JobRestartJobs(void)
{
	Job *job;

	while (!jobFull && (job = TAILQ_FIRST(&stoppedJobs)) != NULL) {
		DEBUGF(JOB, ("Job queue is not full. "
		    "Restarting a stopped job.\n"));
		TAILQ_REMOVE(&stoppedJobs, job, link);
		JobRestart(job);
	}
}

/**
 * Cmd_Exec
 *	Execute the command in cmd, and return the output of that command
 *	in a string.
 *
 * Results:
 *	A string containing the output of the command, or the empty string
 *	If error is not NULL, it contains the reason for the command failure
 *	Any output sent to stderr in the child process is passed to stderr,
 *	and not captured in the string.
 *
 * Side Effects:
 *	The string must be freed by the caller.
 */
Buffer *
Cmd_Exec(const char *cmd, const char **error)
{
	Shell	*shell = commandShell;
	int	fds[2];	/* Pipe streams */
	Buffer	*buf;	/* buffer to store the result */
	ssize_t	rcnt;
	ProcStuff	ps;

	*error = NULL;
	buf = Buf_Init(0);

	/*
	 * Open a pipe for fetching its output
	 */
	if (pipe(fds) == -1) {
		*error = "Couldn't create pipe for \"%s\"";
		return (buf);
	}

	/* Set close-on-exec on read side of pipe. */
	fcntl(fds[0], F_SETFD, fcntl(fds[0], F_GETFD) | FD_CLOEXEC);

	ps.in = STDIN_FILENO;
	ps.out = fds[1];
	ps.err = STDERR_FILENO;

	ps.merge_errors = 0;
	ps.pgroup = 0;
	ps.searchpath = 0;

	/* Set up arguments for shell */
	ps.argv = emalloc(4 * sizeof(char *));
	ps.argv[0] = strdup(shell->name);
	ps.argv[1] = strdup("-c");
	ps.argv[2] = strdup(cmd);
	ps.argv[3] = NULL;
	ps.argv_free = 1;

	/*
	 * Fork.  Warning since we are doing vfork() instead of fork(),
	 * do not allocate memory in the child process!
	 */
	if ((ps.child_pid = vfork()) == -1) {
		*error = "Couldn't exec \"%s\"";

	} else if (ps.child_pid == 0) {
		/*
		 * Child
		 */
		Proc_Exec(&ps, shell);
		/* NOTREACHED */

	} else {
		free(ps.argv[2]);
		free(ps.argv[1]);
		free(ps.argv[0]);
		free(ps.argv);

		close(fds[1]); /* No need for the writing half of the pipe. */

		do {
			char	result[BUFSIZ];

			rcnt = read(fds[0], result, sizeof(result));
			if (rcnt != -1)
				Buf_AddBytes(buf, (size_t)rcnt, result);
		} while (rcnt > 0 || (rcnt == -1 && errno == EINTR));

		if (rcnt == -1)
			*error = "Error reading shell's output for \"%s\"";

		/*
		 * Close the input side of the pipe.
		 */
		close(fds[0]);

		ProcWait(&ps);
		if (ps.child_status)
			*error = "\"%s\" returned non-zero status";

		Buf_StripNewlines(buf);

	}
	return (buf);
}

/**
 * Handle interrupts during the creation of the target and remove
 * it if it ain't precious.  The default handler for the signal is
 * reinstalled, and the signal is raised again.
 *
 * Side Effects:
 *	The target is removed and the process exits.  If the cause was SIGINT
 *	and .INTERRUPT: exists its commands are run.
 */
static void
CompatInterrupt(void)
{
	GNode		*gn;
	int		signo;

	if (got_SIGINT) {
		got_SIGINT = 0;
		signo = SIGINT;

	} else if (got_SIGHUP) {
		got_SIGHUP = 0;
		signo = SIGHUP;

	} else if (got_SIGQUIT) {
		got_SIGQUIT = 0;
		signo = SIGQUIT;

	} else if (got_SIGTERM) {
		got_SIGTERM = 0;
		signo = SIGTERM;

	} else {
		return;		/* no signal delivered */
	}

	if (curTarg != NULL && !Targ_Precious(curTarg)) {
		const char *file = Var_Value(TARGET, curTarg);

		if (!noExecute && eunlink(file) != -1) {
			printf("*** %s removed\n", file);
		}
	}

	/*
	 * Run .INTERRUPT only if hit with interrupt signal
	 */
	if (signo == SIGINT) {
		gn = Targ_FindNode(".INTERRUPT", TARG_NOCREATE);
		if (gn != NULL) {
			Compat_RunCmds(gn, NULL);
		}
	}

	if (signo == SIGQUIT) {
		/*
		 * We do not raise SIGQUIT, since on systems that create core
		 * files upon receipt of SIGQUIT, the core from make would
		 * conflict with a core file from the command that was
		 * running when the SIGQUIT arrived.
		 * 
		 * This is true even on BSD systems that name the core file
		 * after the program, since we might be calling make
		 * recursively.
		 */
		exit(2);
	}

	signal(signo, SIG_DFL);
	kill(getpid(), signo);

	/* NOTREACHED */
}

/**
 * Execute the next command for a target. If the command returns an
 * error, the node's made field is set to ERROR and creation stops.
 * The node from which the command came is also given. This is used
 * to execute the commands in compat mode and when executing commands
 * with the '+' flag in non-compat mode. In these modes each command
 * line should be executed by its own shell. We do some optimization here:
 * if the shell description defines both a string of meta characters and
 * a list of builtins and the command line neither contains a meta character
 * nor starts with one of the builtins then we execute the command directly
 * without invoking a shell.
 *
 * Results:
 *	0 if the command succeeded, 1 if an error occurred.
 *
 * Side Effects:
 *	The node's 'made' field may be set to ERROR.
 */
static int
Compat_RunCommand(GNode *gn, const char cmd[], GNode *ENDNode)
{
	Shell		*shell = commandShell;
	ArgArray	aa;
	char		*cmdStart;	/* Start of expanded command */
	bool		silent;		/* Don't print command */
	bool		doit;		/* Execute even in -n */
	bool		errCheck;	/* Check errors */
	int		status;		/* Description of child's death */
	LstNode		*cmdNode;	/* Node where current cmd is located */
	char		**av;		/* Argument vector for thing to exec */
	ProcStuff	ps;
	char		*line;

	cmdNode = Lst_Member(&gn->commands, cmd);

	cmdStart = Buf_Peel(Var_Subst(cmd, gn, false));
	if (cmdStart[0] == '\0') {
		free(cmdStart);
		Error("%s expands to empty string", cmd);
		return (0);
	}

	Lst_Replace(cmdNode, cmdStart);

	if ((gn->type & OP_SAVE_CMDS) && (gn != ENDNode)) {
		if (ENDNode != NULL) {
			Lst_AtEnd(&ENDNode->commands, cmdStart);
		}
		return (0);
	}

	if (strcmp(cmdStart, "...") == 0) {
		gn->type |= OP_SAVE_CMDS;
		return (0);	/* any further commands are deferred */
	}

	line = cmdStart;
	silent = gn->type & OP_SILENT;
	doit = false;
	errCheck = !(gn->type & OP_IGNORE);

	while (*line == '@' || *line == '-' || *line == '+') {
		switch (*line) {

		case '@':
			silent = DEBUG(LOUD) ? false : true;
			break;

		case '-':
			errCheck = false;
			break;

		case '+':
			doit = true;
			break;
		}
		line++;
	}

	while (isspace((unsigned char)*line))
		line++;

	if (noExecute && doit == false) {
		/* Just print out the command */
		printf("%s\n", line);
		fflush(stdout);
		return (0);
	}

	if (silent == false) {
		/*
		 * Print the command before echoing if we're not supposed to
		 * be quiet for this one.
		 */
		printf("%s\n", line);
		fflush(stdout);
	}

	if (shell->meta == NULL || strpbrk(line, shell->meta) == NULL) {
		char **p;
		char **sh_builtin = shell->builtins.argv + 1;

		/*
		 * Break the command into words to form an argument
		 * vector we can execute.
		 */
		brk_string(&aa, line, true);
		av = aa.argv + 1;

		for (p = sh_builtin; *p != NULL; p++) {
			if (strcmp(av[0], *p) == 0) {
				/*
				 * This command must be passed by the shell
				 * for other reasons.. or.. possibly not at
				 * all.
				 */
				av = NULL;
				break;
			}
		}
	} else {
		/*
		 * We found a "meta" character and need to pass the command
		 * off to the shell.
		 */
		av = NULL;
	}

	ps.in = STDIN_FILENO;
	ps.out = STDOUT_FILENO;
	ps.err = STDERR_FILENO;

	ps.merge_errors = 0;
	ps.pgroup = 0;
	ps.searchpath = 1;

	if (av == NULL) {
		/*
		 * We give the shell the -e flag as well as -c if it's
		 * supposed to exit when it hits an error.
		 */
		ps.argv = emalloc(4 * sizeof(char *));
		ps.argv[0] = strdup(shell->path);
		ps.argv[1] = strdup(errCheck ? "-ec" : "-c");
		ps.argv[2] = strdup(line);
		ps.argv[3] = NULL;
		ps.argv_free = 1;
	} else {
		ps.argv = av;
		ps.argv_free = 0;
	}
	ps.errCheck = errCheck;

	/*
	 * Fork and execute the single command. If the fork fails, we abort.
	 * Warning since we are doing vfork() instead of fork(),
	 * do not allocate memory in the child process!
	 */
	if ((ps.child_pid = vfork()) == -1) {
		Fatal("Could not fork");

	} else if (ps.child_pid == 0) {
		/*
		 * Child
		 */
		Proc_Exec(&ps, shell);
		/* NOTREACHED */

	} else {
		if (ps.argv_free) {
			free(ps.argv[2]);
			free(ps.argv[1]);
			free(ps.argv[0]);
			free(ps.argv);
		} else {
			ArgArray_Done(&aa);
		}

		/*
		 * we need to print out the command associated with this
		 * Gnode in Targ_PrintCmd from Targ_PrintGraph when debugging
		 * at level g2, in main(), Fatal() and DieHorribly(),
		 * therefore do not free it when debugging.
		 */
		if (!DEBUG(GRAPH2)) {
			free(cmdStart);
		}

		/*
		 * The child is off and running. Now all we can do is wait...
		 * Block until child has terminated, or a signal is received.
		 */
		while (ProcWait(&ps) < 0) {
			CompatInterrupt();	/* check on the signal */
		}

		/*
		 * Decode and report the reason child exited, then
		 * indicate how we handled it.
		 */
		if (WIFEXITED(ps.child_status)) {
			status = WEXITSTATUS(ps.child_status);
			if (status == 0) {
				return (0);
			} else {
				printf("*** Error code %d", status);
			}
		} else if (WIFSTOPPED(ps.child_status)) {
			/* can't happen since WUNTRACED isn't set */
			status = WSTOPSIG(ps.child_status);
		} else {
			status = WTERMSIG(ps.child_status);
			printf("*** Signal %d", status);
		}

		if (ps.errCheck) {
			gn->made = ERROR;
			if (keepgoing) {
				/*
				 * Abort the current
				 * target, but let
				 * others continue.
				 */
				printf(" (continuing)\n");
			}
			return (status);
		} else {
			/*
			 * Continue executing
			 * commands for this target.
			 * If we return 0, this will
			 * happen...
			 */
			printf(" (ignored)\n");
			return (0);
		}
	}
}

/**
 * CompatMake
 *	Make a target, given the parent, to abort if necessary.
 *
 * Side Effects:
 *	If an error is detected and not being ignored, the process exits.
 */
static void
CompatMake(GNode *gn, GNode *pgn, GNode *ENDNode, bool queryFlag)
{
	LstNode	*ln;

	if (gn->type & OP_USE) {
		Make_HandleUse(gn, pgn);

	} else if (gn->made == UNMADE) {
		/*
		 * First mark ourselves to be made, then apply whatever
		 * transformations the suffix module thinks are necessary.
		 * Once that's done, we can descend and make all our children.
		 * If any of them has an error but the -k flag was given, our
		 * 'make' field will be set false again. This is our signal to
		 * not attempt to do anything but abort our parent as well.
		 */
		gn->make = true;
		gn->made = BEINGMADE;
		Suff_FindDeps(gn);
		LST_FOREACH(ln, &gn->children)
			CompatMake(Lst_Datum(ln), gn, ENDNode, queryFlag);
		if (!gn->make) {
			gn->made = ABORTED;
			pgn->make = false;
			return;
		}

		if (Lst_Member(&gn->iParents, pgn) != NULL) {
			Var_Set(IMPSRC, Var_Value(TARGET, gn), pgn);
		}

		/*
		 * All the children were made ok. Now cmtime contains the
		 * modification time of the newest child, we need to find out
		 * if we exist and when we were modified last. The criteria for
		 * datedness are defined by the Make_OODate function.
		 */
		DEBUGF(MAKE, ("Examining %s...", gn->name));
		if (!Make_OODate(gn)) {
			gn->made = UPTODATE;
			DEBUGF(MAKE, ("up-to-date.\n"));
			return;
		} else {
			DEBUGF(MAKE, ("out-of-date.\n"));
		}

		/*
		 * If the user is just seeing if something is out-of-date,
		 * exit now to tell him/her "yes".
		 */
		if (queryFlag) {
			exit(1);
		}

		/*
		 * We need to be re-made. We also have to make sure we've got
		 * a $? variable. To be nice, we also define the $> variable
		 * using Make_DoAllVar().
		 */
		Make_DoAllVar(gn);

		/*
		 * Alter our type to tell if errors should be ignored or things
		 * should not be printed so Compat_RunCommand knows what to do.
		 */
		if (Targ_Ignore(gn)) {
			gn->type |= OP_IGNORE;
		}
		if (Targ_Silent(gn)) {
			gn->type |= OP_SILENT;
		}

		if (JobCheckCommands(gn, Fatal)) {
			/*
			 * Our commands are ok, but we still have to worry
			 * about the -t flag...
			 */
			if (touchFlag) {
				JobTouch(gn, gn->type & OP_SILENT);
			} else {
				curTarg = gn;
				Compat_RunCmds(gn, ENDNode);
				curTarg = NULL;
			}
		} else {
			gn->made = ERROR;
		}

		if (gn->made != ERROR) {
			/*
			 * If the node was made successfully, mark it so, update
			 * its modification time and timestamp all its parents.
			 * Note that for .ZEROTIME targets, the timestamping
			 * isn't done. This is to keep its state from affecting
			 * that of its parent.
			 */
			gn->made = MADE;
#ifndef RECHECK
			/*
			 * We can't re-stat the thing, but we can at least take
			 * care of rules where a target depends on a source that
			 * actually creates the target, but only if it has
			 * changed, e.g.
			 *
			 * parse.h : parse.o
			 *
			 * parse.o : parse.y
			 *	yacc -d parse.y
			 *	cc -c y.tab.c
			 *	mv y.tab.o parse.o
			 *	cmp -s y.tab.h parse.h || mv y.tab.h parse.h
			 *
			 * In this case, if the definitions produced by yacc
			 * haven't changed from before, parse.h won't have been
			 * updated and gn->mtime will reflect the current
			 * modification time for parse.h. This is something of a
			 * kludge, I admit, but it's a useful one..
			 *
			 * XXX: People like to use a rule like
			 *
			 * FRC:
			 *
			 * To force things that depend on FRC to be made, so we
			 * have to check for gn->children being empty as well...
			 */
			if (!Lst_IsEmpty(&gn->commands) ||
			    Lst_IsEmpty(&gn->children)) {
				gn->mtime = now;
			}
#else
			/*
			 * This is what Make does and it's actually a good
			 * thing, as it allows rules like
			 *
			 *	cmp -s y.tab.h parse.h || cp y.tab.h parse.h
			 *
			 * to function as intended. Unfortunately, thanks to
			 * the stateless nature of NFS (and the speed of this
			 * program), there are times when the modification time
			 * of a file created on a remote machine will not be
			 * modified before the stat() implied by the Dir_MTime
			 * occurs, thus leading us to believe that the file
			 * is unchanged, wrecking havoc with files that depend
			 * on this one.
			 *
			 * I have decided it is better to make too much than to
			 * make too little, so this stuff is commented out
			 * unless you're sure it's ok.
			 * -- ardeb 1/12/88
			 */
			if (noExecute || Dir_MTime(gn) == 0) {
				gn->mtime = now;
			}
			if (gn->cmtime > gn->mtime)
				gn->mtime = gn->cmtime;
			DEBUGF(MAKE, ("update time: %s\n",
			    Targ_FmtTime(gn->mtime)));
#endif
			if (!(gn->type & OP_EXEC)) {
				pgn->childMade = true;
				Make_TimeStamp(pgn, gn);
			}

		} else if (keepgoing) {
			pgn->make = false;

		} else {
			printf("\n\nStop in %s.\n", Var_Value(".CURDIR", gn));
			exit(1);
		}
	} else if (gn->made == ERROR) {
		/*
		 * Already had an error when making this beastie. Tell the
		 * parent to abort.
		 */
		pgn->make = false;
	} else {
		if (Lst_Member(&gn->iParents, pgn) != NULL) {
			Var_Set(IMPSRC, Var_Value(TARGET, gn), pgn);
		}
		switch(gn->made) {
		  case BEINGMADE:
			Error("Graph cycles through %s\n", gn->name);
			gn->made = ERROR;
			pgn->make = false;
			break;
		  case MADE:
			if ((gn->type & OP_EXEC) == 0) {
			    pgn->childMade = true;
			    Make_TimeStamp(pgn, gn);
			}
			break;
		  case UPTODATE:
			if ((gn->type & OP_EXEC) == 0) {
			    Make_TimeStamp(pgn, gn);
			}
			break;
		  default:
			break;
		}
	}
}

/**
 * Start making given a list of target nodes.  Returns what the exit
 * status of make should be.
 *
 * @note Obviously some function we call is exiting since the code only
 *	 returns 0.  We will fix that bug eventually.
 */
int
Compat_Run(Lst *targs, bool queryFlag)
{
	int	error_cnt;	/* Number of targets not remade due to errors */
	GNode	*deferred;

	deferred = Targ_NewGN("Deferred");

	/*
	 * If the user has defined a .BEGIN target, execute the commands
	 * attached to it.
	 */
	if (queryFlag == false) {
		GNode *gn = Targ_FindNode(".BEGIN", TARG_NOCREATE);
		if (gn != NULL) {
			Compat_RunCmds(gn, deferred);
			if (gn->made == ERROR) {
				printf("\n\nStop.\n");
				return (1);	/* Failed .BEGIN target */
			}
		}
	}

	/*
	 * For each entry in the list of targets to create, call CompatMake on
	 * it to create the thing. CompatMake will leave the 'made' field of gn
	 * in one of several states:
	 *	UPTODATE  gn was already up-to-date
	 *	MADE	  gn was recreated successfully
	 *	ERROR	  An error occurred while gn was being created
	 *	ABORTED	  gn was not remade because one of its inferiors
	 *		  could not be made due to errors.
	 */
	error_cnt = 0;
	while (!Lst_IsEmpty(targs)) {
		GNode *gn = Lst_DeQueue(targs);

		CompatMake(gn, gn, deferred, queryFlag);
		if (gn->made == UPTODATE) {
			printf("`%s' is up to date.\n", gn->name);
		} else if (gn->made == ABORTED) {
			printf("`%s' not remade because of errors.\n",
			    gn->name);
			error_cnt += 1;
		}
	}

	if ((error_cnt == 0) && (queryFlag == false)) {
		GNode *gn;

		/*
		 * If the user has deferred commands using "..." run them.
		 */
		Compat_RunCmds(deferred, NULL);
		if (deferred->made == ERROR) {
			printf("\n\nStop.\n");
			return (1);	/* Failed "deferred" target */
		}

		/*
		 * If the user has defined a .END target, run its commands.
		 */
		gn = Targ_FindNode(".END", TARG_NOCREATE);
		if (gn != NULL) {
			Compat_RunCmds(gn, NULL);
			if (gn->made == ERROR) {
				printf("\n\nStop.\n");
				return (1);	/* Failed .END target */
			}
		}
	}

	return (0);	/* Successful completion */
}

