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
 * $DragonFly: src/lib/libcaps/i386/td_switch.c,v 1.2 2004/07/29 08:55:02 dillon Exp $
 */
#include <sys/cdefs.h>			/* for __dead2 needed by thread.h */
#include "libcaps/thread.h"
#include <sys/thread.h>
#include "libcaps/globaldata.h"		/* for curthread */

void cpu_lwkt_switch(struct thread *);
void cpu_kthread_start(struct thread *);
void cpu_exit_switch(struct thread *);

void
cpu_init_thread(struct thread *td)
{
    td->td_sp = td->td_kstack + td->td_kstack_size - sizeof(void *);
    td->td_switch = cpu_lwkt_switch;
}

void
cpu_set_thread_handler(thread_t td, void (*rfunc)(void), void (*func)(void *), void *arg)
{
	td->td_sp -= sizeof(void *);
	*(void **)td->td_sp = arg;	/* argument to function */
	td->td_sp -= sizeof(void *);
	*(void **)td->td_sp = rfunc;	/* exit function on return */
	td->td_sp -= sizeof(void *);
	*(void **)td->td_sp = func;	/* started by cpu_kthread_start */
	td->td_sp -= sizeof(void *);
	*(void **)td->td_sp = cpu_kthread_start; /* bootstrap */
	td->td_switch = cpu_lwkt_switch;
}

void
cpu_thread_exit(void)
{
	curthread->td_switch = cpu_exit_switch;
	lwkt_switch();
	panic("cpu_exit");
}
