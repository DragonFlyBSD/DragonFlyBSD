/*
 * Copyright (c) 1988, 1993
 *	The Regents of the University of California.  All rights reserved.
 * Copyright (c) 2004 The DragonFly Project.  All rights reserved.
 *
 * This code is derived from software written by Ken Arnold and
 * published in UNIX Review, Vol. 6, No. 8.
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
 * $FreeBSD: src/lib/libc/gen/popen.c,v 1.14 2000/01/27 23:06:19 jasone Exp $
 * $Id: popen.c,v 1.6 2005/02/06 06:57:30 cpressey Exp $
 *
 * @(#)popen.c	8.3 (Berkeley) 5/3/95
 *
 * A modified version of the standard popen()/pclose() functions
 * which adds a third function, pgetpid(), which allows the program
 * which used popen() to obtain the pid of the process on the other
 * end of the pipe.
 */

#include <sys/param.h>
#include <sys/wait.h>
#include <sys/types.h>
#include <sys/time.h>

#include <errno.h>
#include <signal.h>
#include <stdarg.h>
#include <paths.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "popen.h"

extern char **environ;

static struct pid {
	struct pid	*next;
	FILE		*fp;
	pid_t		pid;
} *pidlist;

FILE *
aura_popen(const char *fmt, const char *type, ...)
{
	FILE *iop;
	struct pid *cur;
	struct pid *p;
	va_list args;
	char *command;
	const char *argv[4];
	int pdes[2], pid;
	volatile enum { READ, WRITE, TWO_WAY } pipedir;

	/*
	 * Lite2 introduced two-way popen() pipes using socketpair().
	 * FreeBSD's pipe() is bidirectional, so we use that.
	 */
	if (strchr(type, '+')) {
		pipedir = TWO_WAY;
	} else {
		if (type[1] != '\0')
			return(NULL);
		if (*type == 'r')
			pipedir = READ;
		else if (*type == 'w')
			pipedir = WRITE;
		else
			return(NULL);
	}

	if (pipe(pdes) < 0)
		return(NULL);

	if ((cur = malloc(sizeof(struct pid))) == NULL) {
		close(pdes[0]);
		close(pdes[1]);
		return(NULL);
	}

	va_start(args, type);
	vasprintf(&command, fmt, args);
	va_end(args);

	argv[0] = "sh";
	argv[1] = "-c";
	argv[2] = command;
	argv[3] = NULL;

	switch (pid = vfork()) {
	case -1:			/* Error. */
		close(pdes[0]);
		close(pdes[1]);
		free(cur);
		free(command);
		return(NULL);
		/* NOTREACHED */
	case 0:				/* Child. */
		if (pipedir == READ || pipedir == TWO_WAY) {
			/*
			 * The dup2() to STDIN_FILENO is repeated to avoid
			 * writing to pdes[1], which might corrupt the
			 * parent's copy.  This isn't good enough in
			 * general, since the _exit() is no return, so
			 * the compiler is free to corrupt all the local
			 * variables.
			 */
			close(pdes[0]);
			if (pdes[1] != STDOUT_FILENO) {
				dup2(pdes[1], STDOUT_FILENO);
				close(pdes[1]);
				if (pipedir == TWO_WAY)
					dup2(STDOUT_FILENO, STDIN_FILENO);
			} else if (pipedir == TWO_WAY &&
				   (pdes[1] != STDIN_FILENO)) {
				dup2(pdes[1], STDIN_FILENO);
			}
		} else {
			if (pdes[0] != STDIN_FILENO) {
				dup2(pdes[0], STDIN_FILENO);
				close(pdes[0]);
			}
			close(pdes[1]);
		}
		for (p = pidlist; p; p = p->next) {
			close(fileno(p->fp));
		}
		execve(_PATH_BSHELL, __DECONST(char **, argv), environ);
		_exit(127);
		/* NOTREACHED */
	}

	/* Parent; assume fdopen can't fail. */
	if (pipedir == READ || pipedir == TWO_WAY) {
		iop = fdopen(pdes[0], type);
		close(pdes[1]);
	} else {
		iop = fdopen(pdes[1], type);
		close(pdes[0]);
	}

	/* Link into list of file descriptors. */
	cur->fp = iop;
	cur->pid =  pid;
	cur->next = pidlist;
	pidlist = cur;

	free(command);
	return(iop);
}

/*
 * pclose --
 *	Pclose returns -1 if stream is not associated with a `popened' command,
 *	if already `pclosed', or waitpid returns an error.
 */
int
aura_pclose(FILE *iop)
{
	struct pid *cur, *last;
	int pstat;
	pid_t pid;

	/* Find the appropriate file pointer. */
	for (last = NULL, cur = pidlist; cur; last = cur, cur = cur->next) {
		if (cur->fp == iop)
			break;
	}

	if (cur == NULL)
		return (-1);

	fclose(iop);

	do {
		pid = wait4(cur->pid, &pstat, 0, (struct rusage *)0);
	} while (pid == -1 && errno == EINTR);

	/* Remove the entry from the linked list. */
	if (last == NULL)
		pidlist = cur->next;
	else
		last->next = cur->next;
	free(cur);

	return (pid == -1 ? -1 : pstat);
}

pid_t
aura_pgetpid(FILE *iop)
{
	struct pid *cur;

	/* Find the appropriate file pointer. */
	for (cur = pidlist; cur; cur = cur->next)
		if (cur->fp == iop)
			break;
	if (cur == NULL)
		return (-1);
	return(cur->pid);
}

/*
 * Returns:
 *	1 if all went well.
 *	0 if an error occurred, in which case err is set to:
 *		AURA_PGETS_TIMEOUT:	select() timed out.
 *		AURA_PGETS_SELECT_ERR:	a select() error occurred.
 *		AURA_PGETS_EOF:		end of file condition on pipe.
 *		AURA_PGETS_FGETS_ERR:	a fgets() error occurred.
 */
int
aura_pgets(FILE *p, char *buf, size_t len, long msec, int *err)
{
	struct timeval tv;
	struct timeval *tvp;
	fd_set r;
	int n;

	tv.tv_sec = msec / 1000;
	tv.tv_usec = (msec % 1000) * 1000;
	tvp = (msec < 0 ? NULL : &tv);

	FD_ZERO(&r);
	FD_SET(fileno(p), &r);

	*err = 0;
	buf[0] = '\0';

	if (feof(p)) {
		*err = AURA_PGETS_EOF;
		return(0);
	}
	if (ferror(p)) {
		*err = AURA_PGETS_FGETS_ERR;
		return(0);
	}

	n = select(fileno(p) + 1, &r, NULL, NULL, tvp);

	if (n == 0) {
		*err = AURA_PGETS_TIMEOUT;
		return(0);
	} else if (n < 0) {
		*err = AURA_PGETS_SELECT_ERR;
		return(0);
	} else {
		/* Data came in; read it. */
		if (fgets(buf, len, p) == NULL) {
			if (feof(p)) {
				*err = AURA_PGETS_EOF;
			} else {
				*err = AURA_PGETS_FGETS_ERR;
			}
			return(0);
		} else {
			return(1);
		}
	}
}
