/*
 * Copyright (c) 1999 Peter Wemm <peter@FreeBSD.org>
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 * $FreeBSD: src/sys/sys/kthread.h,v 1.2 2000/01/07 08:36:44 luoqi Exp $
 * $DragonFly: src/sys/sys/kthread.h,v 1.7 2004/07/29 09:02:32 dillon Exp $
 */

#ifndef _SYS_KTHREAD_H_
#define _SYS_KTHREAD_H_

struct proc;
struct thread;

/* 
 * A kernel process descriptor; used to start "internal" daemons
 * 
 * Note: global_procpp may be NULL for no global save area
 */
struct kproc_desc {
	char		*arg0;			/* arg 0 (for 'ps' listing) */
	void		(*func) (void);		/* "main" for kernel process */
	struct thread	**global_threadpp;	/* ptr to proc ptr save area */
};

void	kproc_start (const void *);

int	suspend_kproc (struct thread *, int);
int	resume_kproc (struct thread *);
void	kproc_suspend_loop (void);
void	shutdown_kproc (void *, int);
int	kthread_create (void (*)(void *), void *, struct thread **,
			const char *, ...);
int	kthread_create_stk (void (*)(void *), void *, struct thread **,
			int, const char *, ...);
void	kthread_exit (void) __dead2;


#endif
