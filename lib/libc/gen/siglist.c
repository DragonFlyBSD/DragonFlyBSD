/*-
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 1983, 1993
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
 * @(#)siglist.c	8.1 (Berkeley) 6/4/93
 * $FreeBSD: head/lib/libc/gen/siglist.c 335898 2018-07-03 17:31:45Z jhb $
 */

#include <signal.h>

const char *const sys_signame[NSIG] = {
	[0] =		"Signal 0",
	[SIGHUP] =	"HUP",
	[SIGINT] =	"INT",
	[SIGQUIT] =	"QUIT",
	[SIGILL] =	"ILL",
	[SIGTRAP] =	"TRAP",
	[SIGABRT] =	"ABRT",
	[SIGEMT] =	"EMT",
	[SIGFPE] =	"FPE",
	[SIGKILL] =	"KILL",
	[SIGBUS] =	"BUS",
	[SIGSEGV] =	"SEGV",
	[SIGSYS] =	"SYS",
	[SIGPIPE] =	"PIPE",
	[SIGALRM] =	"ALRM",
	[SIGTERM] =	"TERM",
	[SIGURG] =	"URG",
	[SIGSTOP] =	"STOP",
	[SIGTSTP] =	"TSTP",
	[SIGCONT] =	"CONT",
	[SIGCHLD] =	"CHLD",
	[SIGTTIN] =	"TTIN",
	[SIGTTOU] =	"TTOU",
	[SIGIO] =	"IO",
	[SIGXCPU] =	"XCPU",
	[SIGXFSZ] =	"XFSZ",
	[SIGVTALRM] =	"VTALRM",
	[SIGPROF] =	"PROF",
	[SIGWINCH] =	"WINCH",
	[SIGINFO] =	"INFO",
	[SIGUSR1] =	"USR1",
	[SIGUSR2] =	"USR2",

	/* 32-63 */

	[SIGTHR] =	"THR",
	[SIGCKPT] =	"CKPT",
	[SIGCKPTEXIT] =	"CKPTEXIT",
	[35] =		"Signal 35",
	[36] =		"Signal 36",
	[37] =		"Signal 37",
	[38] =		"Signal 38",
	[39] =		"Signal 39",
	[40] =		"Signal 40",
	[41] =		"Signal 41",
	[42] =		"Signal 42",
	[43] =		"Signal 43",
	[44] =		"Signal 44",
	[45] =		"Signal 45",
	[46] =		"Signal 46",
	[47] =		"Signal 47",
	[48] =		"Signal 48",
	[49] =		"Signal 49",
	[50] =		"Signal 50",
	[51] =		"Signal 51",
	[52] =		"Signal 52",
	[53] =		"Signal 53",
	[54] =		"Signal 54",
	[55] =		"Signal 55",
	[56] =		"Signal 56",
	[57] =		"Signal 57",
	[58] =		"Signal 58",
	[59] =		"Signal 59",
	[60] =		"Signal 60",
	[61] =		"Signal 61",
	[62] =		"Signal 62",
	[63] =		"Signal 63"
};

const char *const sys_siglist[NSIG] = {
	[0] =		"Signal 0",
	[SIGHUP] =	"Hangup",
	[SIGINT] =	"Interrupt",
	[SIGQUIT] =	"Quit",
	[SIGILL] =	"Illegal instruction",
	[SIGTRAP] =	"Trace/BPT trap",
	[SIGABRT] =	"Abort trap",
	[SIGEMT] =	"EMT trap",
	[SIGFPE] =	"Floating point exception",
	[SIGKILL] =	"Killed",
	[SIGBUS] =	"Bus error",
	[SIGSEGV] =	"Segmentation fault",
	[SIGSYS] =	"Bad system call",
	[SIGPIPE] =	"Broken pipe",
	[SIGALRM] =	"Alarm clock",
	[SIGTERM] =	"Terminated",
	[SIGURG] =	"Urgent I/O condition",
	[SIGSTOP] =	"Suspended (signal)",
	[SIGTSTP] =	"Suspended",
	[SIGCONT] =	"Continued",
	[SIGCHLD] =	"Child exited",
	[SIGTTIN] =	"Stopped (tty input)",
	[SIGTTOU] =	"Stopped (tty output)",
	[SIGIO] =	"I/O possible",
	[SIGXCPU] =	"Cputime limit exceeded",
	[SIGXFSZ] =	"Filesize limit exceeded",
	[SIGVTALRM] =	"Virtual timer expired",
	[SIGPROF] =	"Profiling timer expired",
	[SIGWINCH] =	"Window size changes",
	[SIGINFO] =	"Information request",
	[SIGUSR1] =	"User defined signal 1",
	[SIGUSR2] =	"User defined signal 2",

	/* 32-63 */

	[SIGTHR] =	"Thread Scheduler",
	[SIGCKPT] =	"CheckPoint",
	[SIGCKPTEXIT] =	"CheckPointExit",
	[35] =		"Signal 35",
	[36] =		"Signal 36",
	[37] =		"Signal 37",
	[38] =		"Signal 38",
	[39] =		"Signal 39",
	[40] =		"Signal 40",
	[41] =		"Signal 41",
	[42] =		"Signal 42",
	[43] =		"Signal 43",
	[44] =		"Signal 44",
	[45] =		"Signal 45",
	[46] =		"Signal 46",
	[47] =		"Signal 47",
	[48] =		"Signal 48",
	[49] =		"Signal 49",
	[50] =		"Signal 50",
	[51] =		"Signal 51",
	[52] =		"Signal 52",
	[53] =		"Signal 53",
	[54] =		"Signal 54",
	[55] =		"Signal 55",
	[56] =		"Signal 56",
	[57] =		"Signal 57",
	[58] =		"Signal 58",
	[59] =		"Signal 59",
	[60] =		"Signal 60",
	[61] =		"Signal 61",
	[62] =		"Signal 62",
	[63] =		"Signal 63"
};
const int sys_nsig = sizeof(sys_siglist) / sizeof(sys_siglist[0]);
