/*
 * (MPSAFE)
 *
 * Copyright (c) 1982, 1986, 1989, 1993
 *	The Regents of the University of California.  All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Neither the name of the University nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE REGENTS AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE REGENTS OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 *	@(#)vm_meter.c	8.4 (Berkeley) 1/4/94
 * $FreeBSD: src/sys/vm/vm_meter.c,v 1.34.2.7 2002/10/10 19:28:22 dillon Exp $
 * $DragonFly: src/sys/vm/vm_meter.c,v 1.15 2008/04/28 18:04:08 dillon Exp $
 */

#include <sys/param.h>
#include <sys/proc.h>
#include <sys/systm.h>
#include <sys/kernel.h>
#include <sys/resource.h>
#include <sys/vmmeter.h>

#include <vm/vm.h>
#include <vm/vm_page.h>
#include <vm/vm_extern.h>
#include <vm/vm_param.h>
#include <sys/lock.h>
#include <vm/pmap.h>
#include <vm/vm_map.h>
#include <vm/vm_object.h>
#include <sys/sysctl.h>

struct vmstats vmstats;

static int maxslp = MAXSLP;

SYSCTL_UINT(_vm, VM_V_FREE_MIN, v_free_min,
	CTLFLAG_RW, &vmstats.v_free_min, 0,
	"Minimum number of pages desired free");
SYSCTL_UINT(_vm, VM_V_FREE_TARGET, v_free_target,
	CTLFLAG_RW, &vmstats.v_free_target, 0,
	"Number of pages desired free");
SYSCTL_UINT(_vm, VM_V_FREE_RESERVED, v_free_reserved,
	CTLFLAG_RW, &vmstats.v_free_reserved, 0,
	"Number of pages reserved for deadlock");
SYSCTL_UINT(_vm, VM_V_INACTIVE_TARGET, v_inactive_target,
	CTLFLAG_RW, &vmstats.v_inactive_target, 0,
	"Number of pages desired inactive");
SYSCTL_UINT(_vm, VM_V_CACHE_MIN, v_cache_min,
	CTLFLAG_RW, &vmstats.v_cache_min, 0,
	"Min number of pages desired on cache queue");
SYSCTL_UINT(_vm, VM_V_CACHE_MAX, v_cache_max,
	CTLFLAG_RW, &vmstats.v_cache_max, 0,
	"Max number of pages in cached obj");
SYSCTL_UINT(_vm, VM_V_PAGEOUT_FREE_MIN, v_pageout_free_min,
	CTLFLAG_RW, &vmstats.v_pageout_free_min, 0,
	"Min number pages reserved for kernel");
SYSCTL_UINT(_vm, OID_AUTO, v_free_severe,
	CTLFLAG_RW, &vmstats.v_free_severe, 0, "");

SYSCTL_STRUCT(_vm, VM_LOADAVG, loadavg, CTLFLAG_RD, 
    &averunnable, loadavg, "Machine loadaverage history");

static int do_vmtotal_callback(struct proc *p, void *data);

/*
 * No requirements.
 */
static int
do_vmtotal(SYSCTL_HANDLER_ARGS)
{
	struct vmtotal total;
	struct vmtotal *totalp;
	struct vm_object marker;
	vm_object_t object;
	long collisions;
	int burst;

	bzero(&total, sizeof(total));
	totalp = &total;
	bzero(&marker, sizeof(marker));
	marker.type = OBJT_MARKER;
	collisions = vmobj_token.t_collisions;

#if 0
	/*
	 * Mark all objects as inactive.
	 */
	lwkt_gettoken(&vmobj_token);
	for (object = TAILQ_FIRST(&vm_object_list);
	    object != NULL;
	    object = TAILQ_NEXT(object,object_list)) {
		if (object->type == OBJT_MARKER)
			continue;
		vm_object_clear_flag(object, OBJ_ACTIVE);
	}
	lwkt_reltoken(&vmobj_token);
#endif

	/*
	 * Calculate process statistics.
	 */
	allproc_scan(do_vmtotal_callback, totalp);

	/*
	 * Calculate object memory usage statistics.
	 */
	lwkt_gettoken(&vmobj_token);
	TAILQ_INSERT_HEAD(&vm_object_list, &marker, object_list);
	burst = 0;

	for (object = TAILQ_FIRST(&vm_object_list);
	    object != NULL;
	    object = TAILQ_NEXT(object, object_list)) {
		/*
		 * devices, like /dev/mem, will badly skew our totals.
		 * markers aren't real objects.
		 */
		if (object->type == OBJT_MARKER)
			continue;
		if (object->type == OBJT_DEVICE)
			continue;
		if (object->type == OBJT_MGTDEVICE)
			continue;
		if (object->size >= 0x7FFFFFFF) {
			/*
			 * Probably unbounded anonymous memory (really
			 * bounded by related vm_map_entry structures which
			 * we do not have access to in this loop).
			 */
			totalp->t_vm += object->resident_page_count;
		} else {
			/*
			 * It's questionable how useful this is but...
			 */
			totalp->t_vm += object->size;
		}
		totalp->t_rm += object->resident_page_count;
		if (object->flags & OBJ_ACTIVE) {
			totalp->t_avm += object->size;
			totalp->t_arm += object->resident_page_count;
		}
		if (object->shadow_count > 1) {
			/* shared object */
			totalp->t_vmshr += object->size;
			totalp->t_rmshr += object->resident_page_count;
			if (object->flags & OBJ_ACTIVE) {
				totalp->t_avmshr += object->size;
				totalp->t_armshr += object->resident_page_count;
			}
		}

		/*
		 * Don't waste time unnecessarily
		 */
		if (++burst < 25)
			continue;
		burst = 0;

		/*
		 * Don't hog the vmobj_token if someone else wants it.
		 */
		TAILQ_REMOVE(&vm_object_list, &marker, object_list);
		TAILQ_INSERT_AFTER(&vm_object_list, object,
				   &marker, object_list);
		object = &marker;
		if (collisions != vmobj_token.t_collisions) {
			tsleep(&vm_object_list, 0, "breath", 1);
			collisions = vmobj_token.t_collisions;
		} else {
			lwkt_yield();
		}
	}

	TAILQ_REMOVE(&vm_object_list, &marker, object_list);
	lwkt_reltoken(&vmobj_token);

	totalp->t_free = vmstats.v_free_count + vmstats.v_cache_count;

	return (sysctl_handle_opaque(oidp, totalp, sizeof total, req));
}

/*
 * The caller must hold proc_token.
 */
static int
do_vmtotal_callback(struct proc *p, void *data)
{
	struct vmtotal *totalp = data;
	struct lwp *lp;

	if (p->p_flags & P_SYSTEM)
		return(0);

	FOREACH_LWP_IN_PROC(lp, p) {
		switch (lp->lwp_stat) {
		case LSSTOP:
		case LSSLEEP:
			if ((p->p_flags & P_SWAPPEDOUT) == 0) {
				if ((lp->lwp_flags & LWP_SINTR) == 0)
					totalp->t_dw++;
				else if (lp->lwp_slptime < maxslp)
					totalp->t_sl++;
			} else if (lp->lwp_slptime < maxslp) {
				totalp->t_sw++;
			}
			if (lp->lwp_slptime >= maxslp)
				return(0);
			break;

		case LSRUN:
			if (p->p_flags & P_SWAPPEDOUT)
				totalp->t_sw++;
			else
				totalp->t_rq++;
			if (p->p_stat == SIDL)
				return(0);
			break;

		default:
			return (0);
		}

		/*
		 * Set while in vm_fault()
		 */
		if (lp->lwp_flags & LWP_PAGING)
			totalp->t_pw++;
	}
	return(0);
}

/*
 * No requirements.
 */
static int
do_vmstats(SYSCTL_HANDLER_ARGS)
{
	struct vmstats vms = vmstats;
	return (sysctl_handle_opaque(oidp, &vms, sizeof(vms), req));
}

/*
 * No requirements.
 */
static int
do_vmmeter(SYSCTL_HANDLER_ARGS)
{
	int boffset = offsetof(struct vmmeter, vmmeter_uint_begin);
	int eoffset = offsetof(struct vmmeter, vmmeter_uint_end);
	struct vmmeter vmm;
	int i;

	bzero(&vmm, sizeof(vmm));
	for (i = 0; i < ncpus; ++i) {
		int off;
		struct globaldata *gd = globaldata_find(i);

		for (off = boffset; off <= eoffset; off += sizeof(u_int)) {
			*(u_int *)((char *)&vmm + off) +=
				*(u_int *)((char *)&gd->gd_cnt + off);
		}
		
	}
	vmm.v_intr += vmm.v_ipi + vmm.v_timer;
	return (sysctl_handle_opaque(oidp, &vmm, sizeof(vmm), req));
}

/*
 * vcnt() -	accumulate statistics from the cnt structure for each cpu
 *
 *	The vmmeter structure is now per-cpu as well as global.  Those
 *	statistics which can be kept on a per-cpu basis (to avoid cache
 *	stalls between cpus) can be moved to the per-cpu vmmeter.  Remaining
 *	statistics, such as v_free_reserved, are left in the global
 *	structure.
 *
 * (sysctl_oid *oidp, void *arg1, int arg2, struct sysctl_req *req)
 *
 * No requirements.
 */
static int
vcnt(SYSCTL_HANDLER_ARGS)
{
	int i;
	int count = 0;
	int offset = arg2;

	for (i = 0; i < ncpus; ++i) {
		struct globaldata *gd = globaldata_find(i);
		count += *(int *)((char *)&gd->gd_cnt + offset);
	}
	return(SYSCTL_OUT(req, &count, sizeof(int)));
}

/*
 * No requirements.
 */
static int
vcnt_intr(SYSCTL_HANDLER_ARGS)
{
	int i;
	int count = 0;

	for (i = 0; i < ncpus; ++i) {
		struct globaldata *gd = globaldata_find(i);

		count += gd->gd_cnt.v_intr + gd->gd_cnt.v_ipi +
			 gd->gd_cnt.v_timer;
	}
	return(SYSCTL_OUT(req, &count, sizeof(int)));
}

#define VMMETEROFF(var)	offsetof(struct vmmeter, var)

SYSCTL_PROC(_vm, OID_AUTO, vmtotal, CTLTYPE_OPAQUE|CTLFLAG_RD,
    0, sizeof(struct vmtotal), do_vmtotal, "S,vmtotal", 
    "System virtual memory aggregate");
SYSCTL_PROC(_vm, OID_AUTO, vmstats, CTLTYPE_OPAQUE|CTLFLAG_RD,
    0, sizeof(struct vmstats), do_vmstats, "S,vmstats", 
    "System virtual memory statistics");
SYSCTL_PROC(_vm, OID_AUTO, vmmeter, CTLTYPE_OPAQUE|CTLFLAG_RD,
    0, sizeof(struct vmmeter), do_vmmeter, "S,vmmeter", 
    "System statistics");
SYSCTL_NODE(_vm, OID_AUTO, stats, CTLFLAG_RW, 0, "VM meter stats");
SYSCTL_NODE(_vm_stats, OID_AUTO, sys, CTLFLAG_RW, 0, "VM meter sys stats");
SYSCTL_NODE(_vm_stats, OID_AUTO, vm, CTLFLAG_RW, 0, "VM meter vm stats");
SYSCTL_NODE(_vm_stats, OID_AUTO, misc, CTLFLAG_RW, 0, "VM meter misc stats");

SYSCTL_PROC(_vm_stats_sys, OID_AUTO, v_swtch, CTLTYPE_UINT|CTLFLAG_RD,
	0, VMMETEROFF(v_swtch), vcnt, "IU", "Context switches");
SYSCTL_PROC(_vm_stats_sys, OID_AUTO, v_intrans_coll, CTLTYPE_UINT|CTLFLAG_RD,
	0, VMMETEROFF(v_intrans_coll), vcnt, "IU", "Intransit map collisions (total)");
SYSCTL_PROC(_vm_stats_sys, OID_AUTO, v_intrans_wait, CTLTYPE_UINT|CTLFLAG_RD,
	0, VMMETEROFF(v_intrans_wait), vcnt, "IU", "Intransit map collisions which blocked");
SYSCTL_PROC(_vm_stats_sys, OID_AUTO, v_forwarded_ints, CTLTYPE_UINT|CTLFLAG_RD,
	0, VMMETEROFF(v_forwarded_ints), vcnt, "IU", "Forwarded interrupts due to MP lock");
SYSCTL_PROC(_vm_stats_sys, OID_AUTO, v_forwarded_hits, CTLTYPE_UINT|CTLFLAG_RD,
	0, VMMETEROFF(v_forwarded_hits), vcnt, "IU", "Forwarded hits due to MP lock");
SYSCTL_PROC(_vm_stats_sys, OID_AUTO, v_forwarded_misses, CTLTYPE_UINT|CTLFLAG_RD,
	0, VMMETEROFF(v_forwarded_misses), vcnt, "IU", "Forwarded misses due to MP lock");
SYSCTL_PROC(_vm_stats_sys, OID_AUTO, v_trap, CTLTYPE_UINT|CTLFLAG_RD,
	0, VMMETEROFF(v_trap), vcnt, "IU", "Traps");
SYSCTL_PROC(_vm_stats_sys, OID_AUTO, v_syscall, CTLTYPE_UINT|CTLFLAG_RD,
	0, VMMETEROFF(v_syscall), vcnt, "IU", "Syscalls");
SYSCTL_PROC(_vm_stats_sys, OID_AUTO, v_intr, CTLTYPE_UINT|CTLFLAG_RD,
	0, VMMETEROFF(v_intr), vcnt_intr, "IU", "Hardware interrupts");
SYSCTL_PROC(_vm_stats_sys, OID_AUTO, v_ipi, CTLTYPE_UINT|CTLFLAG_RD,
	0, VMMETEROFF(v_ipi), vcnt, "IU", "Inter-processor interrupts");
SYSCTL_PROC(_vm_stats_sys, OID_AUTO, v_timer, CTLTYPE_UINT|CTLFLAG_RD,
	0, VMMETEROFF(v_timer), vcnt, "IU", "LAPIC timer interrupts");
SYSCTL_PROC(_vm_stats_sys, OID_AUTO, v_soft, CTLTYPE_UINT|CTLFLAG_RD,
	0, VMMETEROFF(v_soft), vcnt, "IU", "Software interrupts");
SYSCTL_PROC(_vm_stats_vm, OID_AUTO, v_vm_faults, CTLTYPE_UINT|CTLFLAG_RD,
	0, VMMETEROFF(v_vm_faults), vcnt, "IU", "VM faults");
SYSCTL_PROC(_vm_stats_vm, OID_AUTO, v_cow_faults, CTLTYPE_UINT|CTLFLAG_RD,
	0, VMMETEROFF(v_cow_faults), vcnt, "IU", "COW faults");
SYSCTL_PROC(_vm_stats_vm, OID_AUTO, v_cow_optim, CTLTYPE_UINT|CTLFLAG_RD,
	0, VMMETEROFF(v_cow_optim), vcnt, "IU", "Optimized COW faults");
SYSCTL_PROC(_vm_stats_vm, OID_AUTO, v_zfod, CTLTYPE_UINT|CTLFLAG_RD,
	0, VMMETEROFF(v_zfod), vcnt, "IU", "Zero fill");
SYSCTL_PROC(_vm_stats_vm, OID_AUTO, v_ozfod, CTLTYPE_UINT|CTLFLAG_RD,
	0, VMMETEROFF(v_ozfod), vcnt, "IU", "Optimized zero fill");
SYSCTL_PROC(_vm_stats_vm, OID_AUTO, v_swapin, CTLTYPE_UINT|CTLFLAG_RD,
	0, VMMETEROFF(v_swapin), vcnt, "IU", "Swapin operations");
SYSCTL_PROC(_vm_stats_vm, OID_AUTO, v_swapout, CTLTYPE_UINT|CTLFLAG_RD,
	0, VMMETEROFF(v_swapout), vcnt, "IU", "Swapout operations");
SYSCTL_PROC(_vm_stats_vm, OID_AUTO, v_swappgsin, CTLTYPE_UINT|CTLFLAG_RD,
	0, VMMETEROFF(v_swappgsin), vcnt, "IU", "Swapin pages");
SYSCTL_PROC(_vm_stats_vm, OID_AUTO, v_swappgsout, CTLTYPE_UINT|CTLFLAG_RD,
	0, VMMETEROFF(v_swappgsout), vcnt, "IU", "Swapout pages");
SYSCTL_PROC(_vm_stats_vm, OID_AUTO, v_vnodein, CTLTYPE_UINT|CTLFLAG_RD,
	0, VMMETEROFF(v_vnodein), vcnt, "IU", "Vnodein operations");
SYSCTL_PROC(_vm_stats_vm, OID_AUTO, v_vnodeout, CTLTYPE_UINT|CTLFLAG_RD,
	0, VMMETEROFF(v_vnodeout), vcnt, "IU", "Vnodeout operations");
SYSCTL_PROC(_vm_stats_vm, OID_AUTO, v_vnodepgsin, CTLTYPE_UINT|CTLFLAG_RD,
	0, VMMETEROFF(v_vnodepgsin), vcnt, "IU", "Vnodein pages");
SYSCTL_PROC(_vm_stats_vm, OID_AUTO, v_vnodepgsout, CTLTYPE_UINT|CTLFLAG_RD,
	0, VMMETEROFF(v_vnodepgsout), vcnt, "IU", "Vnodeout pages");
SYSCTL_PROC(_vm_stats_vm, OID_AUTO, v_intrans, CTLTYPE_UINT|CTLFLAG_RD,
	0, VMMETEROFF(v_intrans), vcnt, "IU", "In transit page blocking");
SYSCTL_PROC(_vm_stats_vm, OID_AUTO, v_reactivated, CTLTYPE_UINT|CTLFLAG_RD,
	0, VMMETEROFF(v_reactivated), vcnt, "IU", "Reactivated pages");
SYSCTL_PROC(_vm_stats_vm, OID_AUTO, v_pdwakeups, CTLTYPE_UINT|CTLFLAG_RD,
	0, VMMETEROFF(v_pdwakeups), vcnt, "IU", "Pagedaemon wakeups");
SYSCTL_PROC(_vm_stats_vm, OID_AUTO, v_ppwakeups, CTLTYPE_UINT|CTLFLAG_RD,
	0, VMMETEROFF(v_ppwakeups), vcnt, "IU", "vm_wait wakeups");
SYSCTL_PROC(_vm_stats_vm, OID_AUTO, v_pdpages, CTLTYPE_UINT|CTLFLAG_RD,
	0, VMMETEROFF(v_pdpages), vcnt, "IU", "Pagedaemon page scans");
SYSCTL_PROC(_vm_stats_vm, OID_AUTO, v_dfree, CTLTYPE_UINT|CTLFLAG_RD,
	0, VMMETEROFF(v_dfree), vcnt, "IU", "Pages freed by daemon");
SYSCTL_PROC(_vm_stats_vm, OID_AUTO, v_pfree, CTLTYPE_UINT|CTLFLAG_RD,
	0, VMMETEROFF(v_pfree), vcnt, "IU", "Pages freed by exiting processes");
SYSCTL_PROC(_vm_stats_vm, OID_AUTO, v_tfree, CTLTYPE_UINT|CTLFLAG_RD,
	0, VMMETEROFF(v_tfree), vcnt, "IU", "Total pages freed");
SYSCTL_PROC(_vm_stats_vm, OID_AUTO, v_forks, CTLTYPE_UINT|CTLFLAG_RD,
	0, VMMETEROFF(v_forks), vcnt, "IU", "Number of fork() calls");
SYSCTL_PROC(_vm_stats_vm, OID_AUTO, v_vforks, CTLTYPE_UINT|CTLFLAG_RD,
	0, VMMETEROFF(v_vforks), vcnt, "IU", "Number of vfork() calls");
SYSCTL_PROC(_vm_stats_vm, OID_AUTO, v_rforks, CTLTYPE_UINT|CTLFLAG_RD,
	0, VMMETEROFF(v_rforks), vcnt, "IU", "Number of rfork() calls");
SYSCTL_PROC(_vm_stats_vm, OID_AUTO, v_kthreads, CTLTYPE_UINT|CTLFLAG_RD,
	0, VMMETEROFF(v_kthreads), vcnt, "IU", "Number of fork() calls by kernel");
SYSCTL_PROC(_vm_stats_vm, OID_AUTO, v_forkpages, CTLTYPE_UINT|CTLFLAG_RD,
	0, VMMETEROFF(v_forkpages), vcnt, "IU", "VM pages affected by fork()");
SYSCTL_PROC(_vm_stats_vm, OID_AUTO, v_vforkpages, CTLTYPE_UINT|CTLFLAG_RD,
	0, VMMETEROFF(v_vforkpages), vcnt, "IU", "VM pages affected by vfork()");
SYSCTL_PROC(_vm_stats_vm, OID_AUTO, v_rforkpages, CTLTYPE_UINT|CTLFLAG_RD,
	0, VMMETEROFF(v_rforkpages), vcnt, "IU", "VM pages affected by rfork()");
SYSCTL_PROC(_vm_stats_vm, OID_AUTO, v_kthreadpages, CTLTYPE_UINT|CTLFLAG_RD,
	0, VMMETEROFF(v_kthreadpages), vcnt, "IU", "VM pages affected by fork() by kernel");

SYSCTL_UINT(_vm_stats_vm, OID_AUTO,
	v_page_size, CTLFLAG_RD, &vmstats.v_page_size, 0,
	"Page size in bytes");
SYSCTL_UINT(_vm_stats_vm, OID_AUTO,
	v_page_count, CTLFLAG_RD, &vmstats.v_page_count, 0, 
	"Total number of pages in system");
SYSCTL_UINT(_vm_stats_vm, OID_AUTO,
	v_free_reserved, CTLFLAG_RD, &vmstats.v_free_reserved, 0,
	"Number of pages reserved for deadlock");
SYSCTL_UINT(_vm_stats_vm, OID_AUTO,
	v_free_target, CTLFLAG_RD, &vmstats.v_free_target, 0,
	"Number of pages desired free");
SYSCTL_UINT(_vm_stats_vm, OID_AUTO,
	v_free_min, CTLFLAG_RD, &vmstats.v_free_min, 0,
	"Minimum number of pages desired free");
SYSCTL_UINT(_vm_stats_vm, OID_AUTO,
	v_free_count, CTLFLAG_RD, &vmstats.v_free_count, 0,
	"Number of pages free");
SYSCTL_UINT(_vm_stats_vm, OID_AUTO,
	v_wire_count, CTLFLAG_RD, &vmstats.v_wire_count, 0,
	"Number of pages wired down");
SYSCTL_UINT(_vm_stats_vm, OID_AUTO,
	v_active_count, CTLFLAG_RD, &vmstats.v_active_count, 0,
	"Number of pages active");
SYSCTL_UINT(_vm_stats_vm, OID_AUTO,
	v_inactive_target, CTLFLAG_RD, &vmstats.v_inactive_target, 0,
	"Number of pages desired inactive");
SYSCTL_UINT(_vm_stats_vm, OID_AUTO,
	v_inactive_count, CTLFLAG_RD, &vmstats.v_inactive_count, 0,
	"Number of pages inactive");
SYSCTL_UINT(_vm_stats_vm, OID_AUTO,
	v_cache_count, CTLFLAG_RD, &vmstats.v_cache_count, 0,
	"Number of pages on buffer cache queue");
SYSCTL_UINT(_vm_stats_vm, OID_AUTO,
	v_cache_min, CTLFLAG_RD, &vmstats.v_cache_min, 0,
	"Min number of pages desired on cache queue");
SYSCTL_UINT(_vm_stats_vm, OID_AUTO,
	v_cache_max, CTLFLAG_RD, &vmstats.v_cache_max, 0,
	"Max number of pages in cached obj");
SYSCTL_UINT(_vm_stats_vm, OID_AUTO,
	v_pageout_free_min, CTLFLAG_RD, &vmstats.v_pageout_free_min, 0,
	"Min number pages reserved for kernel");
SYSCTL_UINT(_vm_stats_vm, OID_AUTO,
	v_interrupt_free_min, CTLFLAG_RD, &vmstats.v_interrupt_free_min, 0,
	"Reserved number of pages for int code");
SYSCTL_INT(_vm_stats_misc, OID_AUTO,
	zero_page_count, CTLFLAG_RD, &vm_page_zero_count, 0,
	"Number of zeroing pages");

/*
 * No requirements.
 */
static int
do_vmmeter_pcpu(SYSCTL_HANDLER_ARGS)
{
	int boffset = offsetof(struct vmmeter, vmmeter_uint_begin);
	int eoffset = offsetof(struct vmmeter, vmmeter_uint_end);
	struct globaldata *gd = arg1;
	struct vmmeter vmm;
	int off;

	bzero(&vmm, sizeof(vmm));
	for (off = boffset; off <= eoffset; off += sizeof(u_int)) {
		*(u_int *)((char *)&vmm + off) +=
			*(u_int *)((char *)&gd->gd_cnt + off);
	}
	vmm.v_intr += vmm.v_ipi + vmm.v_timer;
	return (sysctl_handle_opaque(oidp, &vmm, sizeof(vmm), req));
}

/*
 * Called from the low level boot code only.
 */
static void
vmmeter_init(void *dummy __unused)
{
	int i;

	for (i = 0; i < ncpus; ++i) {
		struct sysctl_ctx_list *ctx;
		struct sysctl_oid *oid;
		struct globaldata *gd;
		char name[32];

		ksnprintf(name, sizeof(name), "cpu%d", i);

		ctx = kmalloc(sizeof(*ctx), M_TEMP, M_WAITOK);
		sysctl_ctx_init(ctx);
		oid = SYSCTL_ADD_NODE(ctx, SYSCTL_STATIC_CHILDREN(_vm),
				      OID_AUTO, name, CTLFLAG_RD, 0, "");

		gd = globaldata_find(i);
		SYSCTL_ADD_PROC(ctx, SYSCTL_CHILDREN(oid), OID_AUTO,
				"vmmeter", CTLTYPE_OPAQUE|CTLFLAG_RD,
				gd, sizeof(struct vmmeter), do_vmmeter_pcpu,
				"S,vmmeter", "System per-cpu statistics");
	}
}
SYSINIT(vmmeter, SI_SUB_PSEUDO, SI_ORDER_ANY, vmmeter_init, 0);
