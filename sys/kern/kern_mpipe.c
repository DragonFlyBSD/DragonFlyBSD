/*
 * Copyright (c) 2003 Matthew Dillon <dillon@backplane.com>
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
 * $DragonFly: src/sys/kern/kern_mpipe.c,v 1.3 2004/03/29 14:06:31 joerg Exp $
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/kernel.h>
#include <sys/slaballoc.h>
#include <sys/mbuf.h>
#include <sys/vmmeter.h>
#include <sys/lock.h>
#include <sys/thread.h>
#include <sys/globaldata.h>
#include <sys/mpipe.h>

#include <sys/thread2.h>

void	mpipe_rebalance(malloc_pipe_t mpipe);

typedef struct mpipe_buf {
	TAILQ_ENTRY(mpipe_buf)	entry;
} *mpipe_buf_t;

/*
 * Initialize a malloc pipeline for the specified malloc type and allocation
 * size, and immediately allocate nnow buffers and set the nominal maximum
 * to nmax.
 */
void
mpipe_init(malloc_pipe_t mpipe, malloc_type_t type, int bytes,
	   int global_nnom, int global_nmax, int cpu_nmax,
	   int mpflags)
{
    struct mpipe_buf *buf;
    int i;
    int mflags;

    if (bytes < sizeof(struct mpipe_buf))
	bytes = sizeof(struct mpipe_buf);

    if (global_nnom < cpu_nmax * ncpus)
	global_nnom = cpu_nmax * ncpus;
    if (global_nnom > global_nmax)
	global_nmax = global_nnom;

    bzero(mpipe, sizeof(struct malloc_pipe));
    mpipe->type = type;
    mpipe->bytes = bytes;
    mpipe->max_count = global_nmax;
    mpipe->cpu_max = cpu_nmax;
    mpipe->mpflags = mpflags;

    mflags = M_WAITOK;
    if ((mpflags & MPF_NO_ZERO) == 0)
	mflags |= M_ZERO;

    for (i = 0; i <= SMP_MAXCPU; i++)
	TAILQ_INIT(&mpipe->queue[i]);

    for (i = 1; i <= ncpus; i++) {	
	while (mpipe->queue_len[i] < mpipe->cpu_max) {
	    buf = malloc(mpipe->bytes, mpipe->type, mflags);
	    TAILQ_INSERT_TAIL(&mpipe->queue[i], buf, entry);
	    ++mpipe->total_count;
	    ++mpipe->queue_len[i];
	    --global_nnom;
	}
    }

    TAILQ_INIT(&mpipe->queue[0]);
    while (--global_nnom >= 0) {
	buf = malloc(mpipe->bytes, mpipe->type, mflags);
	TAILQ_INSERT_TAIL(&mpipe->queue[0], buf, entry);
	++mpipe->total_count;
	++mpipe->queue_len[0];
    }
}

void
mpipe_done(malloc_pipe_t mpipe)
{
    mpipe_buf_t buf;
    lwkt_tokref ilock;
    int i;

    lwkt_gettoken(&ilock, &mpipe->mpipe_token);
    KKASSERT(mpipe->queue_len[0] == mpipe->total_count);
    for (i = 0; i < SMP_MAXCPU; i++) {
	while(! TAILQ_EMPTY(&mpipe->queue[i])) {
	    buf = TAILQ_FIRST(&mpipe->queue[i]);
	    KKASSERT(buf != NULL);
	    TAILQ_REMOVE(&mpipe->queue[i], buf, entry);
	    --mpipe->queue_len[i];
	    --mpipe->total_count;
	    free(buf, mpipe->type);
	}
	KKASSERT(mpipe->queue_len[i] == 0);
    }
    KKASSERT(mpipe->total_count == 0);
}

/*
 * Allocation from MPIPE that can wait. Only drain the global queue.
 */
void *
mpipe_alloc_waitok(malloc_pipe_t mpipe)
{
    mpipe_buf_t buf = NULL;
    lwkt_tokref ilock;
    int mflags = M_WAITOK;

    lwkt_gettoken(&ilock, &mpipe->mpipe_token);
    for (;;) {
        crit_enter();

	if (mpipe->queue_len[0] > 0) {
	    buf = TAILQ_FIRST(&mpipe->queue[0]);
            KKASSERT(buf != NULL);
    	    TAILQ_REMOVE(&mpipe->queue[0], buf, entry);
    	    --mpipe->queue_len[0];
	    if ((mpipe->mpflags & MPF_NO_ZERO) == 0)
    		bzero(buf, mpipe->bytes);
	    crit_exit();
	    lwkt_reltoken(&ilock);
	    mpipe_rebalance(mpipe);
	    return(buf);
	}

	if (mpipe->total_count < mpipe->max_count) {
	    if ((mpipe->mpflags & MPF_NO_ZERO) == 0)
		mflags |= M_ZERO;

    	    mpipe->total_count++;
	    crit_exit();
	    lwkt_reltoken(&ilock);
	    buf = malloc(mpipe->bytes, mpipe->type, mflags);
	    KKASSERT(buf != NULL);
	}
        mpipe->pending = 1;
	tsleep(mpipe, 0, "mpipe", 0);
    }
}

/*
 * Allocation from MPIPE that can't wait. Try to drain the
 * local cpu queue first, if that is empty, drain the global
 * CPU
 */

void *
mpipe_alloc_nowait(malloc_pipe_t mpipe)
{
    globaldata_t gd = mycpu;
    mpipe_buf_t buf = NULL;
    lwkt_tokref ilock;
    int my_queue = gd->gd_cpuid + 1;
    int mflags = M_NOWAIT;

    /* First check the local CPU queue to avoid token acquisation. */
    crit_enter();
    if (mpipe->queue_len[my_queue] > 0) {
	buf = TAILQ_FIRST(&mpipe->queue[my_queue]);
        KKASSERT(buf != NULL);
    	TAILQ_REMOVE(&mpipe->queue[my_queue], buf, entry);
    	--mpipe->queue_len[my_queue];
	if ((mpipe->mpflags & MPF_NO_ZERO) == 0)
    	    bzero(buf, mpipe->bytes);
	mpipe_rebalance(mpipe);
	crit_exit();
	return(buf);
    }
    /* We have to acquire the token, unblock interrupts and get it. */
    crit_exit();

    lwkt_gettoken(&ilock, &mpipe->mpipe_token);
    crit_enter();

    if (mpipe->queue_len[0] > 0) {
	buf = TAILQ_FIRST(&mpipe->queue[0]);
        KKASSERT(buf != NULL);
    	TAILQ_REMOVE(&mpipe->queue[0], buf, entry);
    	--mpipe->queue_len[0];
	if ((mpipe->mpflags & MPF_NO_ZERO) == 0)
    	    bzero(buf, mpipe->bytes);
	crit_exit();
	lwkt_reltoken(&ilock);
	return(buf);
    }

    /* Recheck the local CPU queue again in case an interrupt freed something*/
    if (mpipe->queue_len[my_queue] > 0) {
	buf = TAILQ_FIRST(&mpipe->queue[my_queue]);
        KKASSERT(buf != NULL);
    	TAILQ_REMOVE(&mpipe->queue[my_queue], buf, entry);
    	--mpipe->queue_len[my_queue];
	if ((mpipe->mpflags & MPF_NO_ZERO) == 0)
    	    bzero(buf, mpipe->bytes);
	crit_exit();
	lwkt_reltoken(&ilock);
	return(buf);
    }

    if (mpipe->total_count < mpipe->max_count) {
        if ((mpipe->mpflags & MPF_NO_ZERO) == 0)
    	    mflags |= M_ZERO;

	buf = malloc(mpipe->bytes, mpipe->type, mflags);
	if (buf)
    	    mpipe->total_count++;
    }

    crit_exit();
    lwkt_reltoken(&ilock);
    return(buf);
}

/*
 * Free an entry, unblock any waiters.
 */
void
mpipe_free(malloc_pipe_t mpipe, void *vbuf)
{
    globaldata_t gd = mycpu;
    mpipe_buf_t buf = NULL;
    lwkt_tokref ilock;
    int my_queue = gd->gd_cpuid + 1;

    if (vbuf == NULL)
	return;

    lwkt_gettoken(&ilock, &mpipe->mpipe_token);
    crit_enter();

    /* first try to refill the current CPU queue */
    if (mpipe->queue_len[my_queue] < mpipe->cpu_max) {
	TAILQ_INSERT_TAIL(&mpipe->queue[my_queue], buf, entry);
	++mpipe->queue_len[my_queue];
	crit_exit();
	if (mpipe->pending) {
	    mpipe->pending = 0;
	    wakeup(mpipe);
	}
	lwkt_reltoken(&ilock);
	mpipe_rebalance(mpipe);
	return;
    }

    if (mpipe->total_count < mpipe->max_count) {
        TAILQ_INSERT_TAIL(&mpipe->queue[0], buf, entry);
        ++mpipe->queue_len[0];
        crit_exit();
	if (mpipe->pending) {
	    mpipe->pending = 0;
	    wakeup(mpipe);
	}
	lwkt_reltoken(&ilock);
	mpipe_rebalance(mpipe);
	return;
    }

    --mpipe->total_count;
    crit_exit();
    lwkt_reltoken(&ilock);
    free(buf, mpipe->type);    
}

/*
 * Rebalance local CPU queue by trying to size it to max_cpu entries
 */

void
mpipe_rebalance(malloc_pipe_t mpipe)
{
    globaldata_t gd = mycpu;
    mpipe_buf_t buf;
    lwkt_tokref ilock;
    int my_queue = gd->gd_cpuid + 1;

    lwkt_gettoken(&ilock, &mpipe->mpipe_token);
    crit_enter();
    while (mpipe->queue_len[my_queue] < mpipe->cpu_max &&
           mpipe->queue_len[0] > 0) {
	buf = TAILQ_FIRST(&mpipe->queue[0]);
	TAILQ_REMOVE(&mpipe->queue[0], buf, entry);
	TAILQ_INSERT_TAIL(&mpipe->queue[my_queue], buf, entry);
	++mpipe->queue_len[my_queue];
	--mpipe->queue_len[0];
    }
    while (mpipe->queue_len[my_queue] > mpipe->cpu_max) {
	buf = TAILQ_FIRST(&mpipe->queue[my_queue]);
	TAILQ_REMOVE(&mpipe->queue[my_queue], buf, entry);
	TAILQ_INSERT_TAIL(&mpipe->queue[0], buf, entry);
	++mpipe->queue_len[0];
	--mpipe->queue_len[my_queue];
    }
    crit_exit();
    lwkt_reltoken(&ilock);
}
