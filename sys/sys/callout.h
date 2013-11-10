/*-
 * Copyright (c) 1990, 1993
 *	The Regents of the University of California.  All rights reserved.
 * (c) UNIX System Laboratories, Inc.
 * All or some portions of this file are derived from material licensed
 * to the University of California by American Telephone and Telegraph
 * Co. or Unix System Laboratories, Inc. and are reproduced herein with
 * the permission of UNIX System Laboratories, Inc.
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
 *	@(#)callout.h	8.2 (Berkeley) 1/21/94
 * $FreeBSD: src/sys/sys/callout.h,v 1.15.2.1 2001/11/13 18:24:52 archie Exp $
 */

#ifndef _SYS_CALLOUT_H_
#define _SYS_CALLOUT_H_

#include <sys/queue.h>

SLIST_HEAD(callout_list, callout);
TAILQ_HEAD(callout_tailq, callout);

struct callout {
	union {
		SLIST_ENTRY(callout) sle;
		TAILQ_ENTRY(callout) tqe;
	} c_links;
	int	c_time;			/* ticks to the event */
	void	*c_arg;			/* function argument */
	void	(*c_func) (void *);	/* function to call */
	int	c_flags;		/* state of this entry */
	struct globaldata *c_gd;
};

#define CALLOUT_LOCAL_ALLOC	0x0001 /* was allocated from callfree */
#define CALLOUT_ACTIVE		0x0002 /* callout is currently active */
#define CALLOUT_PENDING		0x0004 /* callout is waiting for timeout */
#define CALLOUT_MPSAFE		0x0008 /* callout does not need the BGL */
#define CALLOUT_DID_INIT	0x0010 /* safety check */
#define CALLOUT_RUNNING		0x0020 /* function execution in progress */

struct callout_handle {
	struct callout *callout;
};

/*
 * WARNING!  These macros may only be used when the state of the callout
 * structure is stable, meaning from within the callback function or after
 * the callback function has been called but the timer has not yet been reset.
 */
#define	callout_active(c)	((c)->c_flags & CALLOUT_ACTIVE)
#define	callout_deactivate(c)	((c)->c_flags &= ~CALLOUT_ACTIVE)
#define	callout_pending(c)	((c)->c_flags & CALLOUT_PENDING)

#ifdef _KERNEL
extern int	ncallout;

struct globaldata;

void	hardclock_softtick(struct globaldata *);
void	callout_init (struct callout *);
void	callout_init_mp (struct callout *);
void	callout_reset (struct callout *, int, void (*)(void *), void *);
int	callout_stop (struct callout *);
void	callout_stop_sync (struct callout *);
void	callout_terminate (struct callout *);
void	callout_reset_bycpu (struct callout *, int, void (*)(void *), void *,
	    int);

#define	callout_drain(x) callout_stop_sync(x)

#endif

#endif /* _SYS_CALLOUT_H_ */
