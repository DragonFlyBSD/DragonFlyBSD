/*
 * Copyright (c) 1982, 1986, 1989, 1993, 1994
 *	The Regents of the University of California.  All rights reserved.
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
 *	@(#)wait.h	8.2 (Berkeley) 7/10/94
 * $FreeBSD: src/sys/sys/wait.h,v 1.11 1999/12/29 04:24:50 peter Exp $
 */

#ifndef _SYS_WAIT_H_
#define	_SYS_WAIT_H_

#include <sys/cdefs.h>
#include <machine/stdint.h>

/*
 * This file holds definitions relevant to the wait4 system call
 * and the alternate interfaces that use it (wait, wait3, waitpid).
 */

/*
 * Macros to test the exit status returned by wait
 * and extract the relevant values.
 */
#if __BSD_VISIBLE
#define	WCOREFLAG	0200
#endif
#define	_W_INT(i)	(i)

#define	_WSTATUS(x)	(_W_INT(x) & 0177)
#define	_WSTOPPED	0177		/* _WSTATUS if process is stopped */
#define	WIFSTOPPED(x)	(_WSTATUS(x) == _WSTOPPED)
#define	WSTOPSIG(x)	(_W_INT(x) >> 8)
#define	WIFSIGNALED(x)	(_WSTATUS(x) != _WSTOPPED && _WSTATUS(x) != 0)
#define	WTERMSIG(x)	(_WSTATUS(x))
#define	WIFEXITED(x)	(_WSTATUS(x) == 0)
#define	WEXITSTATUS(x)	(_W_INT(x) >> 8)
#define	WIFCONTINUED(x)	(x == 19)	/* 19 == SIGCONT */
#if __BSD_VISIBLE
#define	WCOREDUMP(x)	(_W_INT(x) & WCOREFLAG)

#define	W_EXITCODE(ret, sig)	((ret) << 8 | (sig))
#define	W_STOPCODE(sig)		((sig) << 8 | _WSTOPPED)
#endif

/*
 * Option bits for the third argument of wait4.  WNOHANG causes the
 * wait to not hang if there are no stopped or terminated processes, rather
 * returning an error indication in this case (pid==0).  WUNTRACED
 * indicates that the caller should receive status about untraced children
 * which stop due to signals.  If children are stopped and a wait without
 * this option is done, it is as though they were still running... nothing
 * about them is returned.
 */
#define	WNOHANG		0x0001	/* don't hang in wait */
#define	WUNTRACED	0x0002	/* tell about stopped, untraced children */
#define	WCONTINUED	0x0004	/* Report a job control continued process. */
#define	WSTOPPED	WUNTRACED
#define	WNOWAIT		0x0008
#define	WEXITED		0x0010
#define	WTRAPPED	0x0020

#if __BSD_VISIBLE
#define	WLINUXCLONE	0x80000000 /* wait for kthread spawned from linux_clone */

/*
 * Tokens for special values of the "pid" parameter to wait4.
 */
#define	WAIT_ANY	(-1)	/* any process */
#define	WAIT_MYPGRP	0	/* any process in my process group */
#endif /* __BSD_VISIBLE */

#ifndef _ID_T_DECLARED
#define	_ID_T_DECLARED
typedef	__int64_t	id_t;	/* general id, can hold gid/pid/uid_t */
#endif

#ifndef _IDTYPE_T_DECLARED
#define	_IDTYPE_T_DECLARED

/* SEE ALSO SYS/PROCCTL.H */

typedef	enum
#if __BSD_VISIBLE
	idtype
#endif
{
	/*
	 * These names were mostly lifted from Solaris source code and
	 * still use Solaris style naming to avoid breaking any
	 * OpenSolaris code which has been ported to FreeBSD/DragonFly.
	 * There is no clear DragonFly counterpart for all of the names, but
	 * some have a clear correspondence to DragonFly entities.
	 *
	 * The numerical values are kept synchronized with the Solaris
	 * values.
	 */
	P_PID,			/* A process identifier. */
	P_PPID,			/* A parent process identifier.	*/
	P_PGID,			/* A process group identifier. */
	P_SID,			/* A session identifier. */
	P_CID,			/* A scheduling class identifier. */
	P_UID,			/* A user identifier. */
	P_GID,			/* A group identifier. */
	P_ALL,			/* All processes. */
	P_LWPID,		/* An LWP identifier. */
	P_TASKID,		/* A task identifier. */
	P_PROJID,		/* A project identifier. */
	P_POOLID,		/* A pool identifier. */
	P_JAILID,		/* A zone identifier. */
	P_CTID,			/* A (process) contract identifier. */
	P_CPUID,		/* CPU identifier. */
	P_PSETID		/* Processor set identifier. */
} idtype_t;			/* The type of id_t we are using. */

#endif

#if !defined(_KERNEL) || defined(_KERNEL_VIRTUAL)
#include <sys/_siginfo.h>

#ifndef _PID_T_DECLARED
typedef	__pid_t		pid_t;		/* process id */
#define	_PID_T_DECLARED
#endif

__BEGIN_DECLS
pid_t	wait(int *);
pid_t	waitpid(pid_t, int *, int);
#if __POSIX_VISIBLE >= 200112
int	waitid(idtype_t, id_t, siginfo_t *, int);
#endif
#if __BSD_VISIBLE || (__XSI_VISIBLE && __XSI_VISIBLE < 700)
struct rusage;
struct __wrusage;
#endif
#if __BSD_VISIBLE || (__XSI_VISIBLE && __XSI_VISIBLE < 600)
pid_t	wait3(int *, int, struct rusage *);
#endif
#if __BSD_VISIBLE
pid_t	wait4(pid_t, int *, int, struct rusage *);
pid_t	wait6(idtype_t, id_t, int *, int, struct __wrusage *,
	    siginfo_t *);
#endif
__END_DECLS
#endif

#endif
