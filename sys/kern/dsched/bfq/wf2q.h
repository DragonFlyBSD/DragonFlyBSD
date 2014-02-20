/*
 * Copyright (c) 2011 The DragonFly Project.  All rights reserved.
 *
 * This code is derived from software contributed to The DragonFly Project
 * by Brills Peng <brillsp@gmail.com>
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

#ifndef _DSCHED_BFQ_WF2Q_H_
#define _DSCHED_BFQ_WF2Q_H_

#include <sys/tree.h>

/* struct bfq_thread_io is defined in bfq.h */
struct bfq_thread_io;

RB_HEAD(wf2q_augtree_t, bfq_thread_io);

struct wf2q_t {
	struct wf2q_augtree_t wf2q_augtree;
	int wf2q_virtual_time;
	int wf2q_tdio_count;
};

#ifndef _DSCHED_BFQ_H_
#include <kern/dsched/bfq/bfq.h>
#endif

void wf2q_init(struct wf2q_t *pwf2q);
void wf2q_insert_thread_io(struct wf2q_t *wf2q, struct bfq_thread_io *tdio);
void wf2q_remove_thread_io(struct wf2q_t *wf2q, struct bfq_thread_io *tdio);
void wf2q_update_vd(struct bfq_thread_io *tdio, int received_service);
struct bfq_thread_io *wf2q_get_next_thread_io(struct wf2q_t *wf2q);
void wf2q_inc_tot_service(struct wf2q_t *wf2q, int amount);

#endif /* !_DSCHED_BFQ_WF2Q_H_ */
