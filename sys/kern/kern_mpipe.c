/*
 * Copyright (c) 2003,2004,2020 The DragonFly Project.  All rights reserved.
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

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/kernel.h>
#include <sys/slaballoc.h>
#include <sys/malloc.h>
#include <sys/mbuf.h>
#include <sys/vmmeter.h>
#include <sys/lock.h>
#include <sys/thread.h>
#include <sys/globaldata.h>
#include <sys/mpipe.h>
#include <sys/kthread.h>

struct mpipe_callback {
	STAILQ_ENTRY(mpipe_callback) entry;
	void (*func)(void *arg1, void *arg2);
	void *arg1;
	void *arg2;
};

static MALLOC_DEFINE(M_MPIPEARY, "MPipe Array", "Auxiliary MPIPE structure");

static void mpipe_thread(void *arg);

/*
 * Initialize a malloc pipeline for the specified malloc type and allocation
 * size.  Create an array to cache up to nom_count buffers and preallocate
 * them.
 */
void
mpipe_init(malloc_pipe_t mpipe, malloc_type_t type, int bytes,
	int nnom, int nmax,
	int mpflags,
	void (*construct)(void *, void *),
	void (*deconstruct)(void *, void *),
	void *priv)
{
    int n;

    if (nnom < 1)
	nnom = 1;
    if (nmax < 0)
	nmax = 0x7FFF0000;	/* some very large number */
    if (nmax < nnom)
	nmax = nnom;
    bzero(mpipe, sizeof(struct malloc_pipe));
    mpipe->type = type;
    mpipe->bytes = bytes;
    mpipe->mpflags = mpflags;
    mpipe->construct = construct;
    mpipe->deconstruct = deconstruct;
    mpipe->priv = priv;
    if ((mpflags & MPF_NOZERO) == 0)
	mpipe->mflags |= M_ZERO;
    if (mpflags & MPF_INT)
	mpipe->mflags |= M_USE_RESERVE | M_USE_INTERRUPT_RESERVE;
    mpipe->ary_count = nnom;
    mpipe->max_count = nmax;
    mpipe->array = kmalloc(nnom * sizeof(mpipe->array[0]), M_MPIPEARY,
			    M_WAITOK | M_ZERO);

    while (mpipe->free_count < nnom) {
	n = mpipe->free_count;
	mpipe->array[n] = kmalloc(bytes, mpipe->type, M_WAITOK | mpipe->mflags);
	if (construct)
	    construct(mpipe->array[n], priv);
	++mpipe->free_count;
	++mpipe->total_count;
    }
    STAILQ_INIT(&mpipe->queue);

    lwkt_token_init(&mpipe->token, "mpipe token");

    /*
     * Create a support thread for the mpipe queue
     */
    if (mpflags & MPF_CALLBACK) {
	    kthread_create(mpipe_thread, mpipe, &mpipe->thread,
			   "mpipe_%s", type->ks_shortdesc);
    }
}

/*
 * Destroy a previously initialized mpipe.  This routine can also safely be
 * called on an uninitialized mpipe structure if it was zero'd or mpipe_done()
 * was previously called on it.
 */
void
mpipe_done(malloc_pipe_t mpipe)
{
    void *buf;
    int n;

    KKASSERT(mpipe->free_count == mpipe->total_count);	/* no outstanding mem */

    /*
     * Clean up the kthread
     */
    lwkt_gettoken(&mpipe->token);
    mpipe->mpflags |= MPF_EXITING;
    while (mpipe->thread) {
	wakeup(&mpipe->queue);
	tsleep(mpipe, 0, "mpipex", 1);
    }

    /*
     * Clean up the mpipe buffers
     */
    for (n = mpipe->free_count - 1; n >= 0; --n) {
	buf = mpipe->array[n];
	mpipe->array[n] = NULL;
	KKASSERT(buf != NULL);
	if (mpipe->deconstruct)
	    mpipe->deconstruct(buf, mpipe->priv);
	kfree(buf, mpipe->type);
    }
    mpipe->free_count = 0;
    mpipe->total_count = 0;
    if (mpipe->array) {
	kfree(mpipe->array, M_MPIPEARY);
	mpipe->array = NULL;
    }
    lwkt_reltoken(&mpipe->token);
    lwkt_token_uninit(&mpipe->token);
}

/*
 * mpipe support thread for request failures when mpipe_alloc_callback()
 * is called.
 *
 * Only set MPF_QUEUEWAIT if entries are pending in the queue.  If no entries
 * are pending and a new entry is added, other code will set MPF_QUEUEWAIT
 * for us.
 */
static void
mpipe_thread(void *arg)
{
    malloc_pipe_t mpipe = arg;
    struct mpipe_callback *mcb;

    lwkt_gettoken(&mpipe->token);
    while ((mpipe->mpflags & MPF_EXITING) == 0) {
	while (mpipe->free_count &&
	       (mcb = STAILQ_FIRST(&mpipe->queue)) != NULL) {
		STAILQ_REMOVE(&mpipe->queue, mcb, mpipe_callback, entry);
		mcb->func(mcb->arg1, mcb->arg2);
		kfree(mcb, M_MPIPEARY);
	}
	if (STAILQ_FIRST(&mpipe->queue))
		mpipe->mpflags |= MPF_QUEUEWAIT;
	tsleep(&mpipe->queue, 0, "wait", 0);
    }
    mpipe->thread = NULL;
    lwkt_reltoken(&mpipe->token);
    wakeup(mpipe);
}


/*
 * Allocate an entry (inline support routine).  The allocation is guaranteed
 * to return non-NULL up to the nominal count after which it may return NULL.
 * Note that the implementation is defined to be allowed to block for short
 * periods of time.
 *
 * Use mpipe_alloc_callback() for non-blocking operation with a callback
 * Use mpipe_alloc_nowait() for non-blocking operation without a callback
 * Use mpipe_alloc_waitok() for blocking operation & guaranteed non-NULL
 */
static __inline
void *
_mpipe_alloc_locked(malloc_pipe_t mpipe, int mfailed)
{
    void *buf;
    int n;

    if ((n = mpipe->free_count) != 0) {
	/*
	 * Use a free entry if it exists.
	 */
	--n;
	buf = mpipe->array[n];
	mpipe->array[n] = NULL;	/* sanity check, not absolutely needed */
	mpipe->free_count = n;
    } else if (mpipe->total_count >= mpipe->max_count || mfailed) {
	/*
	 * Return NULL if we have hit our limit
	 */
	buf = NULL;
    } else {
	/*
	 * Otherwise try to malloc() non-blocking.
	 */
	buf = kmalloc(mpipe->bytes, mpipe->type, M_NOWAIT | mpipe->mflags);
	if (buf) {
	    ++mpipe->total_count;
	    if (mpipe->construct)
	        mpipe->construct(buf, mpipe->priv);
	}
    }
    return(buf);
}

/*
 * Nominal non-blocking mpipe allocation
 */
void *
mpipe_alloc_nowait(malloc_pipe_t mpipe)
{
    void *buf;

    lwkt_gettoken(&mpipe->token);
    buf = _mpipe_alloc_locked(mpipe, 0);
    lwkt_reltoken(&mpipe->token);

    return(buf);
}

/*
 * non-blocking mpipe allocation with callback for retry.
 *
 * If NULL is returned func(arg) is queued and will be called back when
 * space is likely (but not necessarily) available.
 *
 * If non-NULL is returned func(arg) is ignored.
 */
void *
mpipe_alloc_callback(malloc_pipe_t mpipe, void (*func)(void *arg1, void *arg2),
		     void *arg1, void *arg2)
{
    struct mpipe_callback *mcb;
    void *buf;

    lwkt_gettoken(&mpipe->token);
    buf = _mpipe_alloc_locked(mpipe, 0);
    if (buf == NULL) {
	mcb = kmalloc(sizeof(*mcb), M_MPIPEARY, M_INTWAIT);
	buf = _mpipe_alloc_locked(mpipe, 0);
	if (buf == NULL) {
	    mcb->func = func;
	    mcb->arg1 = arg1;
	    mcb->arg2 = arg2;
	    STAILQ_INSERT_TAIL(&mpipe->queue, mcb, entry);
	    mpipe->mpflags |= MPF_QUEUEWAIT;	/* for mpipe_thread() */
	} else {
	    kfree(mcb, M_MPIPEARY);
	}
    }
    lwkt_reltoken(&mpipe->token);

    return(buf);
}

/*
 * This function can be called to nominally wait until resources are
 * available and mpipe_alloc_nowait() is likely to return non-NULL.
 *
 * NOTE: mpipe_alloc_nowait() can still return NULL.
 */
void
mpipe_wait(malloc_pipe_t mpipe)
{
    if (mpipe->free_count == 0) {
	lwkt_gettoken(&mpipe->token);
	while ((mpipe->mpflags & MPF_EXITING) == 0) {
	    if (mpipe->free_count)
		    break;
	    mpipe->mpflags |= MPF_QUEUEWAIT;
	    tsleep(&mpipe->queue, 0, "wait", 0);
	}
	lwkt_reltoken(&mpipe->token);
    }
}

/*
 * Allocate an entry, block until the allocation succeeds.  This may cause
 * us to block waiting for a prior allocation to be freed.
 */
void *
mpipe_alloc_waitok(malloc_pipe_t mpipe)
{
    void *buf;
    int mfailed;

    lwkt_gettoken(&mpipe->token);
    mfailed = 0;
    while ((buf = _mpipe_alloc_locked(mpipe, mfailed)) == NULL) {
	/*
	 * Block if we have hit our limit
	 */
	mpipe->pending = 1;
	tsleep(mpipe, 0, "mpipe1", 0);
	mfailed = 1;
    }
    lwkt_reltoken(&mpipe->token);

    return(buf);
}

/*
 * Free an entry, unblock any waiters.  Allow NULL.
 */
void
mpipe_free(malloc_pipe_t mpipe, void *buf)
{
    int n;

    if (buf == NULL)
	return;

    lwkt_gettoken(&mpipe->token);
    if ((n = mpipe->free_count) < mpipe->ary_count) {
	/*
	 * Free slot available in free array (LIFO)
	 */
	mpipe->array[n] = buf;
	++mpipe->free_count;
	if ((mpipe->mpflags & (MPF_CACHEDATA|MPF_NOZERO)) == 0)
	    bzero(buf, mpipe->bytes);
	if (mpipe->mpflags & MPF_QUEUEWAIT) {
		mpipe->mpflags &= ~MPF_QUEUEWAIT;
		lwkt_reltoken(&mpipe->token);
		wakeup(&mpipe->queue);
	} else {
		lwkt_reltoken(&mpipe->token);
	}
	/*
	 * Wakeup anyone blocked in mpipe_alloc_*().
	 */
	if (mpipe->pending) {
	    mpipe->pending = 0;
	    wakeup(mpipe);
	}
    } else {
	/*
	 * All the free slots are full, free the buffer directly.
	 */
	--mpipe->total_count;
	KKASSERT(mpipe->total_count >= mpipe->free_count);
	if (mpipe->deconstruct)
	    mpipe->deconstruct(buf, mpipe->priv);
	lwkt_reltoken(&mpipe->token);
	kfree(buf, mpipe->type);
    }
}

