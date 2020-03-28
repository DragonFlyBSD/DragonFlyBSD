/*
 * Copyright (c) 2020 The DragonFly Project.  All rights reserved.
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
#include "opt_cpu.h"

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/kernel.h>
#include <sys/sysctl.h>
#include <sys/malloc.h>
#include <sys/priv.h>
#include <sys/flame_graph.h>

#include <sys/thread2.h>

#include <machine/atomic.h>
#include <machine/cpufunc.h>
#include <machine/cputypes.h>
#include <machine/psl.h>
#include <machine/segments.h>
#include <machine/tss.h>
#include <machine/specialreg.h>
#include <machine/globaldata.h>

#include <machine/md_var.h>		/* setidt() */

__read_frequently struct flame_graph_pcpu *flame_graph_array;
__read_frequently static int flame_graph_enable;

SYSCTL_INT(_debug, OID_AUTO, flame_graph_enable, CTLFLAG_RW,
	   &flame_graph_enable, 0, "Collect data for flame graphs");
SYSCTL_LONG(_debug, OID_AUTO, flame_graph_array, CTLFLAG_RD,
	   &flame_graph_array, 0, "Collect data for flame graphs");

MALLOC_DEFINE(M_FLAME, "flame_graphs", "Flame Graphs");

static void
hard_sniff_init(void *arg)
{
	struct flame_graph_pcpu *fg;
	struct flame_graph_entry *fge;
	int n;

	flame_graph_array = kmalloc(sizeof(*fg) * ncpus,
				    M_FLAME, M_WAITOK | M_CACHEALIGN | M_ZERO);
	for (n = 0; n < ncpus; ++n) {
		fge = kmalloc(sizeof(*fge) * FLAME_GRAPH_NENTRIES,
			      M_FLAME, M_WAITOK | M_CACHEALIGN | M_ZERO);

		fg = &flame_graph_array[n];
		fg->nentries = FLAME_GRAPH_NENTRIES;
		fg->fge = fge;
	}
}

SYSINIT(swi_vm_setup, SI_BOOT2_MACHDEP, SI_ORDER_ANY, hard_sniff_init, NULL);

/*
 * Xsniff vector calls into here
 *
 * WARNING! This code ignores critical sections!  The system can be
 *          in any state.  The only thing we safely have access to
 *          is the gd.
 */
void
hard_sniff(struct trapframe *tf)
{
        globaldata_t gd = mycpu;
	thread_t td;
	char *top;
	char *bot;
	char *rbp;
	char *rip;
	struct flame_graph_pcpu *fg;
	struct flame_graph_entry *fge;
	int n;

	/*
	 * systat -pv 1 sampling
	 */
        gd->gd_sample_pc = (void *)(intptr_t)tf->tf_rip;
        gd->gd_sample_sp = (void *)(intptr_t)tf->tf_rsp;

	/*
	 * Flame graph sampling, require %rbp (frame pointer) chaining.
	 * Attempt to follow the chain and record what we believe are
	 * %rip addresses.
	 */
	if (flame_graph_enable == 0)
		return;
	td = gd->gd_curthread;
	if (td == NULL)
		return;
	bot = (char *)td->td_kstack + PAGE_SIZE;	/* skip guard */
	top = (char *)td->td_kstack + td->td_kstack_size;
	if (bot >= top)
		return;
	fg = &flame_graph_array[gd->gd_cpuid];
	fge = &fg->fge[fg->windex % FLAME_GRAPH_NENTRIES];

	rip = (char *)(intptr_t)tf->tf_rip;
	fge->rips[0] = (intptr_t)rip;
	rbp = (char *)(intptr_t)tf->tf_rbp;

	for (n = 1; n < FLAME_GRAPH_FRAMES - 1; ++n) {
		if (rbp < bot || rbp > top - 8 || ((intptr_t)rbp & 7))
			break;
		fge->rips[n] = (intptr_t)*(char **)(rbp + 8);
		if (*(char **)rbp <= rbp)
			break;
		rbp = *(char **)rbp;
	}
	fge->rips[n] = 0;
	cpu_sfence();
	++fg->windex;
}

static int
sysctl_flame_graph_data(SYSCTL_HANDLER_ARGS)
{
	int error;
	int n;
	size_t ebytes;

	error = priv_check_cred(curthread->td_ucred, PRIV_ROOT, 0);
	if (error)
		return error;
	if (flame_graph_array == NULL)
		return EOPNOTSUPP;

	ebytes = sizeof(struct flame_graph_pcpu) +
		 sizeof(struct flame_graph_entry);

	for (n = 0; n < ncpus && error == 0; ++n) {
		error = SYSCTL_OUT(req, &ebytes, sizeof(ebytes));
		if (error == 0)
			error = SYSCTL_OUT(req, flame_graph_array + n,
					   sizeof(*flame_graph_array));
		if (error == 0)
			error = SYSCTL_OUT(req, flame_graph_array[n].fge,
					   sizeof(*flame_graph_array->fge) *
					   FLAME_GRAPH_NENTRIES);
	}

	return error;
}

SYSCTL_PROC(_debug, OID_AUTO, flame_graph_data,
	    (CTLTYPE_OPAQUE|CTLFLAG_RD), 0, 0,
	    sysctl_flame_graph_data, "S,flames", "Flame Graph Data");

static int
sysctl_flame_graph_sniff(SYSCTL_HANDLER_ARGS)
{
	int error;

	error = priv_check_cred(curthread->td_ucred, PRIV_ROOT, 0);
	if (error)
		return error;
	if (flame_graph_enable == 0)
		return EINVAL;
	if (req->newptr)
		smp_sniff();
	return(SYSCTL_OUT(req, &error, sizeof(int)));
}

SYSCTL_PROC(_debug, OID_AUTO, flame_graph_sniff,
	    (CTLTYPE_UINT|CTLFLAG_RW), 0, 0,
	    sysctl_flame_graph_sniff, "IU", "Flame Graph Poll");
