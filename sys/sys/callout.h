/*
 * Copyright (c) 2014,2018,2019 The DragonFly Project.  All rights reserved.
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

#ifndef _SYS_CALLOUT_H_
#define _SYS_CALLOUT_H_

#ifndef _SYS_QUEUE_H_
#include <sys/queue.h>
#endif
#ifndef _SYS_LOCK_H_
#include <sys/lock.h>
#endif
#ifndef _SYS_SPINLOCK_H_
#include <sys/spinlock.h>
#endif

/*
 * WITH TYPESTABLE (currently disabled)
 *
 *  This is a mechanism to use a separate internal _callout structure
 *  for kern_timeout's internals instead of embedding.  At the moment
 *  it doesn't work properly because tsleep() is using a temporary callout
 *  and can't afford to block on setup.
 *
 * WITHOUT TYPESTABLE  <---
 *
 *  Without typestable we embed the internal _callout structure within
 *  the external callout structure.  We will still use the verifier to
 *  try to catch corruption.
 */
struct callout;
#undef CALLOUT_TYPESTABLE

struct _callout {
	struct spinlock	spin;
	TAILQ_ENTRY(_callout) entry;
	struct callout	*verifier;
	uint32_t	flags;
	uint32_t	lineno;
	struct lock	*lk;
	const char	*ident;

	struct softclock_pcpu *rsc;	/* request info */
	void		*rarg;
	void		(*rfunc)(void *);
	int		rtick;
	int		unused01;

	struct softclock_pcpu *qsc;	/* active info */
	void		*qarg;
	void		(*qfunc)(void *);
	int		qtick;
	int		waiters;
};

struct callout {
#ifdef CALLOUT_TYPESTABLE
	struct _callout	*toc;		/* opaque internal structure */
	uint32_t	flags;		/* initial flags */
	uint32_t	lineno;		/* debugging */
	struct lock	*lk;		/* interlock */
	const char	*ident;		/* debugging */
	void		*arg;		/* ONLY used for callout_arg() XXX */
#else
	struct _callout	toc;
	uint32_t	flags;
#endif
};

/*
 * Legacy access/setting of the function and argument.  Used only
 * by netgraph7 and ieee80211_dfs.c.  DO NOT USE FOR NEW CODE!
 */
#ifdef CALLOUT_TYPESTABLE
#define callout_set_arg(cc, _arg)	((cc)->qarg = (_arg))
#define callout_arg(cc)			((cc)->qarg)
#define callout_func(cc)		((cc)->qfunc)
#else
#define callout_set_arg(cc, _arg)	((cc)->toc.qarg = (_arg))
#define callout_arg(cc)			((cc)->toc.qarg)
#define callout_func(cc)		((cc)->toc.qfunc)
#endif

#ifdef _KERNEL

#define CALLOUT_DEBUG
#ifdef CALLOUT_DEBUG
#define CALLOUT_DEBUG_ARGS	, const char *ident, int lineno
#define CALLOUT_DEBUG_PASSTHRU	, ident, lineno
#else
#define CALLOUT_DEBUG_ARGS
#define CALLOUT_DEBUG_PASSTHRU
#endif

struct globaldata;

extern int ncallout;

int	callout_active(struct callout *ext);
int	callout_pending(struct callout *ext);
void	callout_deactivate(struct callout *ext);

void	hardclock_softtick(struct globaldata *);
void	_callout_init (struct callout *cc
			CALLOUT_DEBUG_ARGS);
void	_callout_init_mp (struct callout *cc
			CALLOUT_DEBUG_ARGS);
void	_callout_init_lk (struct callout *cc, struct lock *lk
			CALLOUT_DEBUG_ARGS);
void	callout_reset (struct callout *, int,
			void (*)(void *), void *);
void	callout_reset_bycpu (struct callout *, int,
			void (*)(void *), void *,
			int);
int	callout_stop (struct callout *cc);
int	callout_stop_async (struct callout *cc);
void	callout_terminate (struct callout *cc);
int	callout_cancel (struct callout *cc);
int	callout_drain (struct callout *cc);

#ifdef CALLOUT_DEBUG
#define	callout_init(co)	_callout_init(co, __FILE__, __LINE__)
#define	callout_init_mp(co)	_callout_init_mp(co, __FILE__, __LINE__)
#define	callout_init_lk(co, lk)	_callout_init_lk(co, lk, __FILE__, __LINE__)
#else
#define	callout_init(co)	_callout_init(co)
#define	callout_init_mp(co)	_callout_init_mp(co)
#define	callout_init_lk(co, lk)	_callout_init_lk(co, lk)
#endif

#endif	/* _KERNEL */

#endif	/* _SYS_CALLOUT_H_ */
