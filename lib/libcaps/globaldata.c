/*
 * GLOBALDATA.C
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
 * $DragonFly: src/lib/libcaps/globaldata.c,v 1.1 2003/11/24 21:15:58 dillon Exp $
 */

#include "defs.h"

struct globaldata gdary[1];
struct globaldata *mycpu = &gdary[0];
int smp_active;
int ncpus = 1;
char *panicstr;

struct globaldata *
globaldata_find(int cpu)
{
    KKASSERT(cpu >= 0 && cpu < ncpus);
    return(&gdary[0]);
}

void *
libcaps_alloc_stack(int stksize)
{
    return(malloc(stksize));
}

void
libcaps_free_stack(void *stk, int stksize)
{
    free(stk);
}

void
splz(void)
{
}

int
need_resched()
{
    return(0);
}

void
pmap_init_thread(struct thread *td)
{
}

void
cpu_thread_exit(void)
{
    exit(1);
}

void
cpu_set_thread_handler(struct thread *td, void (*exitfunc)(void),
		void (*func)(void *), void *ar)
{
    assert(0);
}

void
panic(const char *ctl, ...)
{
    va_list va;

    va_start(va, ctl);
    vfprintf(stderr, ctl, va);
    va_end(va);
    exit(1);
}
