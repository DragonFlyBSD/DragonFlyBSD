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
 * $DragonFly: src/lib/libcaps/globaldata.c,v 1.2 2003/12/04 22:06:19 dillon Exp $
 */

#include "defs.h"

struct globaldata gdary[MAXVCPU];
u_int mp_lock;
int smp_active;
int ncpus = 1;
u_int32_t stopped_cpus;
char *panicstr;

/*
 * Master globaldata init
 */
void
globaldata_init(thread_t td)
{
    mi_gdinit(&gdary[0], 0);
    if (td) {
	gdary[0].gd_curthread = td;
	lwkt_init_thread(td, NULL, TDF_RUNNING, mycpu);
    }
}

/*
 * per-cpu globaldata init.  Calls lwkt_gdinit() and md_gdinit().  Returns
 * with the target cpu left in a critical section.
 */
void
mi_gdinit(globaldata_t gd, int cpuid)
{
    bzero(gd, sizeof(*gd));
    TAILQ_INIT(&gd->gd_tdfreeq);
    gd->gd_cpuid = cpuid;
    gd->gd_self = gd;
    gd->gd_upcall.magic = UPCALL_MAGIC;
    gd->gd_upcall.crit_count = UPC_CRITADD;
    gd->gd_upcid = upc_register(&gd->gd_upcall, upc_callused_wrapper,
				(void *)lwkt_process_ipiq, gd);
    gd->gd_ipiq = malloc(sizeof(lwkt_ipiq) * MAXVCPU);
    bzero(gd->gd_ipiq, sizeof(lwkt_ipiq) * MAXVCPU);
    if (gd->gd_upcid < 0)
	panic("upc_register: failed on cpu %d\n", cpuid);
    md_gdinit(gd);
    lwkt_gdinit(gd);
}

globaldata_t
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
need_resched(void)
{
    return(0);
}

void
cpu_send_ipiq(int dcpu)
{
    upc_control(UPC_CONTROL_DISPATCH, gdary[dcpu].gd_upcid, (void *)TDPRI_CRIT);
}

__dead2 void
panic(const char *ctl, ...)
{
    va_list va;

    va_start(va, ctl);
    vfprintf(stderr, ctl, va);
    va_end(va);
    abort();
}

