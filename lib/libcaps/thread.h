/*
 * THREAD.H
 *
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
 * $DragonFly: src/lib/libcaps/thread.h,v 1.3 2004/07/29 08:55:02 dillon Exp $
 */

#ifndef _LIBCAPS_THREAD_H_
#define _LIBCAPS_THREAD_H_

#define LWKT_THREAD_STACK	65536

struct thread;

struct md_thread {

};

extern void *libcaps_alloc_stack(int);
extern void libcaps_free_stack(void *, int);
extern int tsleep(struct thread *, int, const char *, int);
extern void lwkt_start_threading(struct thread *);
extern void cpu_init_thread(struct thread *);
extern void cpu_set_thread_handler(struct thread *, void (*)(void), void (*)(void *), void *);
extern void kthread_exit(void) __dead2;
extern void cpu_thread_exit(void) __dead2;

/*
 * User overloads of lwkt_*
 * Unfortunately c doesn't support function overrloading.
 * XXX we need some strong weak magic here....
 */
struct globaldata;
void lwkt_user_gdinit(struct globaldata *);

extern int hz;

#endif

