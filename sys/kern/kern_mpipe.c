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
 * $DragonFly: src/sys/kern/kern_mpipe.c,v 1.1 2003/11/30 20:13:54 dillon Exp $
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/kernel.h>
#include <sys/slaballoc.h>
#include <sys/mpipe.h>
#include <sys/mbuf.h>
#include <sys/vmmeter.h>
#include <sys/lock.h>
#include <sys/thread.h>
#include <sys/globaldata.h>

#include <sys/thread2.h>

#define arysize(ary)	(sizeof(ary)/sizeof((ary)[0]))

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
	int nnow, int nmax)
{
    if (bytes < sizeof(struct mpipe_buf))
	bytes = sizeof(struct mpipe_buf);
    bzero(mpipe, sizeof(struct malloc_pipe));
    TAILQ_INIT(&mpipe->queue);
    mpipe->type = type;
    mpipe->bytes = bytes;
    mpipe->max_count = nmax;
    if (nnow > 0) {
	void *buf;

	buf = malloc(bytes, mpipe->type, M_WAITOK);
	KKASSERT(buf != NULL);
	++mpipe->total_count;
	mpipe_free(mpipe, buf);
	while (--nnow > 0) {
	    buf = malloc(bytes, mpipe->type, M_NOWAIT);
	    if (buf == NULL)
		break;
	    ++mpipe->total_count;
	    mpipe_free(mpipe, buf);
	}
    }
    if (mpipe->max_count < mpipe->total_count)
	mpipe->max_count = mpipe->total_count;
}

void
mpipe_done(malloc_pipe_t mpipe)
{
    struct mpipe_buf *buf;

    KKASSERT(mpipe->free_count == mpipe->total_count);
    while (mpipe->free_count) {
	buf = TAILQ_FIRST(&mpipe->queue);
	KKASSERT(buf != NULL);
	TAILQ_REMOVE(&mpipe->queue, buf, entry);
	--mpipe->free_count;
	--mpipe->total_count;
	free(buf, mpipe->type);
    }
    KKASSERT(TAILQ_EMPTY(&mpipe->queue));
}

/*
 * Allocate an entry.  flags can be M_NOWAIT which tells us not to block.
 * Unlike a normal malloc, if we block in mpipe_alloc() no deadlock will occur
 * because it will unblock the moment an existing in-use buffer is freed.
 */
void *
mpipe_alloc(malloc_pipe_t mpipe, int flags)
{
    mpipe_buf_t buf;

    crit_enter();
    while (mpipe->free_count == 0) {
	if (mpipe->total_count < mpipe->max_count) {
	    ++mpipe->total_count;
	    if ((buf = malloc(mpipe->bytes, mpipe->type, flags)) != NULL) {
		crit_exit();
		return(buf);
	    }
	    --mpipe->total_count;
	} else if (flags & M_NOWAIT) {
	    crit_exit();
	    return(NULL);
	} else {
	    mpipe->pending = 1;
	    tsleep(mpipe, 0, "mpipe", 0);
	}
    }
    buf = TAILQ_FIRST(&mpipe->queue);
    KKASSERT(buf != NULL);
    TAILQ_REMOVE(&mpipe->queue, buf, entry);
    --mpipe->free_count;
    crit_exit();
    if (flags & M_ZERO)
	bzero(buf, mpipe->bytes);
    return(buf);
}

/*
 * Free an entry, unblock any waiters.
 */
void
mpipe_free(malloc_pipe_t mpipe, void *vbuf)
{
    struct mpipe_buf *buf;

    if ((buf = vbuf) != NULL) {
	crit_enter();
	if (mpipe->total_count > mpipe->max_count) {
	    --mpipe->total_count;
	    crit_exit();
	    free(buf, mpipe->type);
	} else {
	    TAILQ_INSERT_TAIL(&mpipe->queue, buf, entry);
	    ++mpipe->free_count;
	    crit_exit();
	    if (mpipe->free_count >= (mpipe->total_count >> 2) + 1) {
		if (mpipe->trigger) {
		    mpipe->trigger(mpipe->trigger_data);
		}
		if (mpipe->pending) {
		    mpipe->pending = 0;
		    wakeup(mpipe);
		}
	    }
	}
    }
}

