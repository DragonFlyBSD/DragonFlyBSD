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
 * $DragonFly: src/sys/sys/mpipe.h,v 1.1 2003/11/30 20:13:53 dillon Exp $
 */

#ifndef _SYS_MPIPE_H_
#define _SYS_MPIPE_H_

#ifndef _SYS_MALLOC_H_
#include <sys/malloc.h>
#endif

/*
 * Pipeline memory allocations.  This implements a pipeline for allocations
 * of a particular size.  It is used in order to allow memory allocations
 * to block while at the same time guarenteeing that no deadlocks will occur.
 */
struct mpipe_buf;

struct malloc_pipe {
    TAILQ_HEAD(, mpipe_buf) queue;
    malloc_type_t type;		/* malloc bucket */
    int		bytes;		/* allocation size */
    int		pending;	/* there is a request pending */
    int		free_count;	/* entries in free list */
    int		total_count;	/* total free + outstanding */
    int		max_count;	/* maximum cache size */
    void	(*trigger)(void *data);	/* trigger function on free */
    void	*trigger_data;
};

typedef struct malloc_pipe *malloc_pipe_t;

#ifdef _KERNEL

void mpipe_init(malloc_pipe_t mpipe, malloc_type_t type,
		int bytes, int nnow, int nmax);
void mpipe_done(malloc_pipe_t mpipe);
void *mpipe_alloc(malloc_pipe_t mpipe, int flags);
void mpipe_free(malloc_pipe_t mpipe, void *vbuf);

#endif

#endif

