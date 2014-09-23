/*
 * Copyright (c) 2003,2004 The DragonFly Project.  All rights reserved.
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
 * 
 * $DragonFly: src/sys/sys/mpipe.h,v 1.4 2004/07/16 05:51:57 dillon Exp $
 */

#ifndef _SYS_MPIPE_H_
#define _SYS_MPIPE_H_

#ifndef _SYS_MALLOC_H_
#include <sys/malloc.h>
#endif
#ifndef _SYS_THREAD_H_
#include <sys/thread.h>
#endif
#ifndef _SYS_QUEUE_H_
#include <sys/queue.h>
#endif

/*
 * Pipeline memory allocations with persistent store capabilities.  This
 * implements a pipeline for allocations of a particular size.  It is used
 * in order to allow memory allocations to block while at the same time
 * guarenteeing that no deadlocks will occur.
 *
 * By default new allocations are zero'd out. 
 *
 * MPF_NOZERO		If specified the underlying buffers are not zero'd.
 *			Note this also means you have no way of knowing which
 *			buffers are coming from the cache and which are new
 *			allocations.
 *
 * MPF_CACHEDATA	If specified the deconstructor will be called when
 *			the underlying buffer is free()'d, but the buffer may
 *			be reused many times before/if that happens.  The
 *			buffer is NOT zero'd on reuse regardless of the
 *			MPF_NOZERO flag.
 *
 *			If not specified and MPF_NOZERO is also not specified,
 *			then buffers reused from the cache will be zero'd as
 *			well as new allocations.
 *
 *			Note that the deconstructor function may still be NULL
 *			if this flag is specified, meaning that you don't need
 *			notification when the cached contents is physically
 *			free()'d.
 *
 * MPF_INT		Use the interrupt reserve if necessary.
 */
struct mpipe_buf;
struct mpipe_callback;

struct malloc_pipe {
    malloc_type_t type;		/* malloc bucket */
    int		bytes;		/* allocation size */
    int		mpflags;	/* MPF_ flags */
    int		mflags;		/* M_ flags (used internally) */
    int		pending;	/* there is a request pending */
    int		free_count;	/* entries in array[] */
    int		total_count;	/* total outstanding allocations incl free */
    int		ary_count;	/* guarenteed allocation count */
    int		max_count;	/* maximum count (M_NOWAIT used beyond nom) */
    lwkt_token	token;
    void	**array;	/* array[ary_count] */
    void	(*construct)(void *buf, void *priv);
    void	(*deconstruct)(void *buf, void *priv);
    void	*priv;
    struct thread *thread;	/* support thread for mpipe */
    STAILQ_HEAD(, mpipe_callback) queue;
};

#define MPF_CACHEDATA		0x0001	/* cache old buffers (do not zero) */ 
#define MPF_NOZERO		0x0002	/* do not zero-out new allocations */
#define MPF_INT			0x0004	/* use the interrupt memory reserve */
#define MPF_QUEUEWAIT		0x0008
#define MPF_CALLBACK		0x0010	/* callback will be used */
#define MPF_EXITING		0x80000000

typedef struct malloc_pipe *malloc_pipe_t;

#ifdef _KERNEL

void mpipe_init(malloc_pipe_t mpipe, malloc_type_t type,
		int bytes, int nnom, int nmax, 
		int mpflags, 
		void (*construct)(void *, void *),
		void (*deconstruct)(void *, void *),
		void *priv);
void mpipe_done(malloc_pipe_t mpipe);
void *mpipe_alloc_waitok(malloc_pipe_t mpipe);
void *mpipe_alloc_nowait(malloc_pipe_t mpipe);
void *mpipe_alloc_callback(malloc_pipe_t mpipe,
		void (*func)(void *arg1, void *arg2), void *arg1, void *arg2);
void mpipe_wait(malloc_pipe_t mpipe);
void mpipe_free(malloc_pipe_t mpipe, void *vbuf);

#endif

#endif
