/*-
 * Copyright (c) 1990, 1993
 *	The Regents of the University of California.  All rights reserved.
 *
 * This code is derived from software contributed to Berkeley by
 * Chris Torek.
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
 * $FreeBSD: src/lib/libc/stdlib/atexit.c,v 1.3.6.1 2002/03/10 20:28:40 tegge Exp $
 * $DragonFly: src/lib/libcr/stdlib/Attic/atexit.c,v 1.4 2004/03/13 19:46:56 eirikn Exp $
 *
 * @(#)atexit.c	8.2 (Berkeley) 7/3/94
 */

#include <stddef.h>
#include <stdlib.h>
#include <unistd.h>
#include "atexit.h"

#include "libc_private.h"
#include "spinlock.h"

#define		ATEXIT_FN_EMPTY	0
#define		ATEXIT_FN_STD	1
#define		ATEXIT_FN_CXA	2

static spinlock_t thread_lock = _SPINLOCK_INITIALIZER;

#define THREAD_LOCK()		if (__isthreaded) _SPINLOCK(&thread_lock);
#define THREAD_UNLOCK()		if (__isthreaded) _SPINUNLOCK(&thread_lock);

struct atexit {
	struct atexit *next;			/* next in list */
	int ind;				/* next index in this table */
	struct atexit_fn {
		int fn_type;			/* ATEXIT_? from above */
		union {
			void (*std_func)(void);
			void (*cxa_func)(void *);
		} fn_ptr;			/* function pointer */
		void *fn_arg;			/* argument for CXA callback */
		void *fn_dso;			/* shared module handle */
	} fns[ATEXIT_SIZE];			/* the table itself */
};

static struct atexit *__atexit;		/* points to head of LIFO stack */

/*
 * Register the function described by 'fptr' to be called at application
 * exit or owning shared object unload time. This is a helper function
 * for atexit and __cxa_atexit.
 */
static int
atexit_register(struct atexit_fn *fptr)
{
	static struct atexit __atexit0;	/* one guaranteed table */
	struct atexit *p;

	THREAD_LOCK();
	if ((p = __atexit) == NULL)
		__atexit = p = &__atexit0;
	else while (p->ind >= ATEXIT_SIZE) {
		struct atexit *old__atexit;
		old__atexit = __atexit;
	        THREAD_UNLOCK();
		if ((p = (struct atexit *)malloc(sizeof(*p))) == NULL)
			return (-1);
		THREAD_LOCK();
		if (old__atexit != __atexit) {
			/* Lost race, retry operation */
			THREAD_UNLOCK();
			free(p);
			THREAD_LOCK();
			p = __atexit;
			continue;
		}
		p->ind = 0;
		p->next = __atexit;
		__atexit = p;
	}
	p->fns[p->ind++] = *fptr;
	THREAD_UNLOCK();
	return (0);
}

/*
 * Register a function to be performed at exit.
 */
int
atexit(void (*func)(void))
{
	struct atexit_fn fn;
	int error;

	fn.fn_type = ATEXIT_FN_STD;
	fn.fn_ptr.std_func = func;;
	fn.fn_arg = NULL;
	fn.fn_dso = NULL;

 	error = atexit_register(&fn);	
	return (error);
}

/*
 * Register a function to be performed at exit or when an shared object
 * with given dso handle is unloaded dynamically.
 */
int
__cxa_atexit(void (*func)(void *), void *arg, void *dso)
{
	struct atexit_fn fn;
	int error;

	fn.fn_type = ATEXIT_FN_CXA;
	fn.fn_ptr.cxa_func = func;;
	fn.fn_arg = arg;
	fn.fn_dso = dso;

 	error = atexit_register(&fn);	
	return (error);
}

/*
 * Call all handlers registered with __cxa_atexit for the shared
 * object owning 'dso'.  Note: if 'dso' is NULL, then all remaining
 * handlers are called.
 */
void
__cxa_finalize(void *dso)
{
	struct atexit *p;
	struct atexit_fn fn;
	int n;

	THREAD_LOCK();
	for (p = __atexit; p; p = p->next) {
		for (n = p->ind; --n >= 0;) {
			if (p->fns[n].fn_type == ATEXIT_FN_EMPTY)
				continue; /* already been called */
			if (dso != NULL && dso != p->fns[n].fn_dso)
				continue; /* wrong DSO */
			fn = p->fns[n];
			/*
			  Mark entry to indicate that this particular handler
			  has already been called.
			*/
			p->fns[n].fn_type = ATEXIT_FN_EMPTY;
		        THREAD_UNLOCK();
		
			/* Call the function of correct type. */
			if (fn.fn_type == ATEXIT_FN_CXA)
				fn.fn_ptr.cxa_func(fn.fn_arg);
			else if (fn.fn_type == ATEXIT_FN_STD)
				fn.fn_ptr.std_func();
			THREAD_LOCK();
		}
	}
	THREAD_UNLOCK();
	return (0);
}
