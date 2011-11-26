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
 * $DragonFly: src/lib/libcaps/globaldata.c,v 1.7 2004/07/29 08:55:02 dillon Exp $
 */

#include "defs.h"

struct globaldata gdary[MAXVCPU];
u_int mp_lock;
int ncpus = 1;
cpumask_t stopped_cpus;
cpumask_t smp_active_mask;
char *panicstr;

/*
 * Master globaldata init
 */
void
globaldata_init(thread_t td)
{
    mi_gdinit1(&gdary[0], 0);
    mi_gdinit2(&gdary[0]);

    /*
     * If a 'main' thread is passed make it the current thread and mark it
     * as currently running, but not on the run queue.
     */
    if (td) {
	gdary[0].gd_curthread = td;
	lwkt_init_thread(td, NULL, 0, TDF_RUNNING|TDF_SYSTHREAD, mycpu);
    }
}

/*
 * per-cpu globaldata init.  Calls lwkt_gdinit() and md_gdinit*().  Returns
 * with the target cpu left in a critical section.
 */
void
mi_gdinit1(globaldata_t gd, int cpuid)
{
    bzero(gd, sizeof(*gd));
    TAILQ_INIT(&gd->gd_tdfreeq);
    gd->gd_cpuid = cpuid;
    gd->gd_cpumask = (cpumask_t)1 << cpuid;
    gd->gd_self = gd;
    gd->gd_upcall.upc_magic = UPCALL_MAGIC;
    gd->gd_upcall.upc_critoff = offsetof(struct thread, td_pri);
    gd->gd_ipiq = malloc(sizeof(lwkt_ipiq) * MAXVCPU);
    bzero(gd->gd_ipiq, sizeof(lwkt_ipiq) * MAXVCPU);
    md_gdinit1(gd);
}

void
mi_gdinit2(globaldata_t gd)
{
    gd->gd_upcid = upc_register(&gd->gd_upcall, upc_callused_wrapper,
				(void *)lwkt_process_ipiq, gd);
    if (gd->gd_upcid < 0)
	panic("upc_register: failed on cpu %d\n", gd->gd_cpuid);
    md_gdinit2(gd);
    lwkt_gdinit(gd);
}

globaldata_t
globaldata_find(int cpu)
{
    KKASSERT(cpu >= 0 && cpu < ncpus);
    return(&gdary[cpu]);
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

/*
 * Process any pending upcalls now.  Remember there is a dispatch interlock
 * if upc_pending is non-zero, so we have to set it to zero.  If we are in
 * a critical section this function is a NOP.
 */
void
splz(void)
{
    globaldata_t gd = mycpu;

    if (gd->gd_upcall.upc_pending) {
	gd->gd_upcall.upc_pending = 0;
	upc_control(UPC_CONTROL_DISPATCH, -1, (void *)1);
    }
}

int
need_lwkt_resched(void)
{
    return(0);
}

void
cpu_send_ipiq(int dcpu)
{
    upc_control(UPC_CONTROL_DISPATCH, gdary[dcpu].gd_upcid, (void *)TDPRI_CRIT);
}

void
cpu_halt(void)
{
    upc_control(UPC_CONTROL_WAIT, -1, (void *)TDPRI_CRIT);
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

/*
 * Create a new virtual cpu.  The cpuid is returned and may be used
 * in later lwkt_create() calls.
 */
static int
caps_vcpu_start(void *vgd)
{
    mi_gdinit2(vgd);	/* sets %gs */
    cpu_rfork_start();
}

int
caps_fork_vcpu(void)
{
    int cpuid;
    globaldata_t gd;
    char stack[8192];

    if ((cpuid = ncpus) >= MAXVCPU)
	return(-1);
    ++ncpus;
    gd = &gdary[cpuid];
    mi_gdinit1(gd, cpuid);
    rfork_thread(RFMEM|RFPROC|RFSIGSHARE, stack + sizeof(stack),
		caps_vcpu_start, gd);
    while (gd->gd_pid == 0)
	;
    /* XXX wait for upcall setup */
    return(cpuid);
}

