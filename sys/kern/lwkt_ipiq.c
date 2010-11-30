/*
 * Copyright (c) 2003,2004 The DragonFly Project.  All rights reserved.
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
 * 
 * $DragonFly: src/sys/kern/lwkt_ipiq.c,v 1.27 2008/05/18 20:57:56 nth Exp $
 */

/*
 * This module implements IPI message queueing and the MI portion of IPI
 * message processing.
 */

#include "opt_ddb.h"

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/kernel.h>
#include <sys/proc.h>
#include <sys/rtprio.h>
#include <sys/queue.h>
#include <sys/thread2.h>
#include <sys/sysctl.h>
#include <sys/ktr.h>
#include <sys/kthread.h>
#include <machine/cpu.h>
#include <sys/lock.h>
#include <sys/caps.h>

#include <vm/vm.h>
#include <vm/vm_param.h>
#include <vm/vm_kern.h>
#include <vm/vm_object.h>
#include <vm/vm_page.h>
#include <vm/vm_map.h>
#include <vm/vm_pager.h>
#include <vm/vm_extern.h>
#include <vm/vm_zone.h>

#include <machine/stdarg.h>
#include <machine/smp.h>
#include <machine/atomic.h>

#ifdef SMP
static __int64_t ipiq_count;	/* total calls to lwkt_send_ipiq*() */
static __int64_t ipiq_fifofull;	/* number of fifo full conditions detected */
static __int64_t ipiq_avoided;	/* interlock with target avoids cpu ipi */
static __int64_t ipiq_passive;	/* passive IPI messages */
static __int64_t ipiq_cscount;	/* number of cpu synchronizations */
static int ipiq_optimized = 1;	/* XXX temporary sysctl */
#ifdef PANIC_DEBUG
static int	panic_ipiq_cpu = -1;
static int	panic_ipiq_count = 100;
#endif
#endif

#ifdef SMP
SYSCTL_QUAD(_lwkt, OID_AUTO, ipiq_count, CTLFLAG_RW, &ipiq_count, 0,
    "Number of IPI's sent");
SYSCTL_QUAD(_lwkt, OID_AUTO, ipiq_fifofull, CTLFLAG_RW, &ipiq_fifofull, 0,
    "Number of fifo full conditions detected");
SYSCTL_QUAD(_lwkt, OID_AUTO, ipiq_avoided, CTLFLAG_RW, &ipiq_avoided, 0,
    "Number of IPI's avoided by interlock with target cpu");
SYSCTL_QUAD(_lwkt, OID_AUTO, ipiq_passive, CTLFLAG_RW, &ipiq_passive, 0,
    "Number of passive IPI messages sent");
SYSCTL_QUAD(_lwkt, OID_AUTO, ipiq_cscount, CTLFLAG_RW, &ipiq_cscount, 0,
    "Number of cpu synchronizations");
SYSCTL_INT(_lwkt, OID_AUTO, ipiq_optimized, CTLFLAG_RW, &ipiq_optimized, 0,
    "");
#ifdef PANIC_DEBUG
SYSCTL_INT(_lwkt, OID_AUTO, panic_ipiq_cpu, CTLFLAG_RW, &panic_ipiq_cpu, 0, "");
SYSCTL_INT(_lwkt, OID_AUTO, panic_ipiq_count, CTLFLAG_RW, &panic_ipiq_count, 0, "");
#endif

#define IPIQ_STRING	"func=%p arg1=%p arg2=%d scpu=%d dcpu=%d"
#define IPIQ_ARG_SIZE	(sizeof(void *) * 2 + sizeof(int) * 3)

#if !defined(KTR_IPIQ)
#define KTR_IPIQ	KTR_ALL
#endif
KTR_INFO_MASTER(ipiq);
KTR_INFO(KTR_IPIQ, ipiq, send_norm, 0, IPIQ_STRING, IPIQ_ARG_SIZE);
KTR_INFO(KTR_IPIQ, ipiq, send_pasv, 1, IPIQ_STRING, IPIQ_ARG_SIZE);
KTR_INFO(KTR_IPIQ, ipiq, send_nbio, 2, IPIQ_STRING, IPIQ_ARG_SIZE);
KTR_INFO(KTR_IPIQ, ipiq, send_fail, 3, IPIQ_STRING, IPIQ_ARG_SIZE);
KTR_INFO(KTR_IPIQ, ipiq, receive, 4, IPIQ_STRING, IPIQ_ARG_SIZE);
KTR_INFO(KTR_IPIQ, ipiq, sync_start, 5, "cpumask=%08x", sizeof(cpumask_t));
KTR_INFO(KTR_IPIQ, ipiq, sync_add, 6, "cpumask=%08x", sizeof(cpumask_t));
KTR_INFO(KTR_IPIQ, ipiq, cpu_send, 7, IPIQ_STRING, IPIQ_ARG_SIZE);
KTR_INFO(KTR_IPIQ, ipiq, send_end, 8, IPIQ_STRING, IPIQ_ARG_SIZE);

#define logipiq(name, func, arg1, arg2, sgd, dgd)	\
	KTR_LOG(ipiq_ ## name, func, arg1, arg2, sgd->gd_cpuid, dgd->gd_cpuid)
#define logipiq2(name, arg)	\
	KTR_LOG(ipiq_ ## name, arg)

#endif	/* SMP */

#ifdef SMP

static int lwkt_process_ipiq_core(globaldata_t sgd, lwkt_ipiq_t ip, 
				  struct intrframe *frame);
static void lwkt_cpusync_remote1(lwkt_cpusync_t poll);
static void lwkt_cpusync_remote2(lwkt_cpusync_t poll);

/*
 * Send a function execution request to another cpu.  The request is queued
 * on the cpu<->cpu ipiq matrix.  Each cpu owns a unique ipiq FIFO for every
 * possible target cpu.  The FIFO can be written.
 *
 * If the FIFO fills up we have to enable interrupts to avoid an APIC
 * deadlock and process pending IPIQs while waiting for it to empty.   
 * Otherwise we may soft-deadlock with another cpu whos FIFO is also full.
 *
 * We can safely bump gd_intr_nesting_level because our crit_exit() at the
 * end will take care of any pending interrupts.
 *
 * The actual hardware IPI is avoided if the target cpu is already processing
 * the queue from a prior IPI.  It is possible to pipeline IPI messages
 * very quickly between cpus due to the FIFO hysteresis.
 *
 * Need not be called from a critical section.
 */
int
lwkt_send_ipiq3(globaldata_t target, ipifunc3_t func, void *arg1, int arg2)
{
    lwkt_ipiq_t ip;
    int windex;
    struct globaldata *gd = mycpu;

    logipiq(send_norm, func, arg1, arg2, gd, target);

    if (target == gd) {
	func(arg1, arg2, NULL);
	logipiq(send_end, func, arg1, arg2, gd, target);
	return(0);
    } 
    crit_enter();
    ++gd->gd_intr_nesting_level;
#ifdef INVARIANTS
    if (gd->gd_intr_nesting_level > 20)
	panic("lwkt_send_ipiq: TOO HEAVILY NESTED!");
#endif
    KKASSERT(curthread->td_critcount);
    ++ipiq_count;
    ip = &gd->gd_ipiq[target->gd_cpuid];

    /*
     * Do not allow the FIFO to become full.  Interrupts must be physically
     * enabled while we liveloop to avoid deadlocking the APIC.
     */
    if (ip->ip_windex - ip->ip_rindex > MAXCPUFIFO / 2) {
#if defined(__i386__)
	unsigned int eflags = read_eflags();
#elif defined(__x86_64__)
	unsigned long rflags = read_rflags();
#endif

	if (atomic_poll_acquire_int(&ip->ip_npoll) || ipiq_optimized == 0) {
	    logipiq(cpu_send, func, arg1, arg2, gd, target);
	    cpu_send_ipiq(target->gd_cpuid);
	}
	cpu_enable_intr();
	++ipiq_fifofull;
	while (ip->ip_windex - ip->ip_rindex > MAXCPUFIFO / 4) {
	    KKASSERT(ip->ip_windex - ip->ip_rindex != MAXCPUFIFO - 1);
	    lwkt_process_ipiq();
	}
#if defined(__i386__)
	write_eflags(eflags);
#elif defined(__x86_64__)
	write_rflags(rflags);
#endif
    }

    /*
     * Queue the new message
     */
    windex = ip->ip_windex & MAXCPUFIFO_MASK;
    ip->ip_func[windex] = func;
    ip->ip_arg1[windex] = arg1;
    ip->ip_arg2[windex] = arg2;
    cpu_sfence();
    ++ip->ip_windex;
    --gd->gd_intr_nesting_level;

    /*
     * signal the target cpu that there is work pending.
     */
    if (atomic_poll_acquire_int(&ip->ip_npoll)) {
	logipiq(cpu_send, func, arg1, arg2, gd, target);
	cpu_send_ipiq(target->gd_cpuid);
    } else {
	if (ipiq_optimized == 0) {
	    logipiq(cpu_send, func, arg1, arg2, gd, target);
	    cpu_send_ipiq(target->gd_cpuid);
	} else {
	    ++ipiq_avoided;
	}
    }
    crit_exit();

    logipiq(send_end, func, arg1, arg2, gd, target);
    return(ip->ip_windex);
}

/*
 * Similar to lwkt_send_ipiq() but this function does not actually initiate
 * the IPI to the target cpu unless the FIFO has become too full, so it is
 * very fast.
 *
 * This function is used for non-critical IPI messages, such as memory
 * deallocations.  The queue will typically be flushed by the target cpu at
 * the next clock interrupt.
 *
 * Need not be called from a critical section.
 */
int
lwkt_send_ipiq3_passive(globaldata_t target, ipifunc3_t func,
			void *arg1, int arg2)
{
    lwkt_ipiq_t ip;
    int windex;
    struct globaldata *gd = mycpu;

    KKASSERT(target != gd);
    crit_enter();
    logipiq(send_pasv, func, arg1, arg2, gd, target);
    ++gd->gd_intr_nesting_level;
#ifdef INVARIANTS
    if (gd->gd_intr_nesting_level > 20)
	panic("lwkt_send_ipiq: TOO HEAVILY NESTED!");
#endif
    KKASSERT(curthread->td_critcount);
    ++ipiq_count;
    ++ipiq_passive;
    ip = &gd->gd_ipiq[target->gd_cpuid];

    /*
     * Do not allow the FIFO to become full.  Interrupts must be physically
     * enabled while we liveloop to avoid deadlocking the APIC.
     */
    if (ip->ip_windex - ip->ip_rindex > MAXCPUFIFO / 2) {
#if defined(__i386__)
	unsigned int eflags = read_eflags();
#elif defined(__x86_64__)
	unsigned long rflags = read_rflags();
#endif

	if (atomic_poll_acquire_int(&ip->ip_npoll) || ipiq_optimized == 0) {
	    logipiq(cpu_send, func, arg1, arg2, gd, target);
	    cpu_send_ipiq(target->gd_cpuid);
	}
	cpu_enable_intr();
	++ipiq_fifofull;
	while (ip->ip_windex - ip->ip_rindex > MAXCPUFIFO / 4) {
	    KKASSERT(ip->ip_windex - ip->ip_rindex != MAXCPUFIFO - 1);
	    lwkt_process_ipiq();
	}
#if defined(__i386__)
	write_eflags(eflags);
#elif defined(__x86_64__)
	write_rflags(rflags);
#endif
    }

    /*
     * Queue the new message
     */
    windex = ip->ip_windex & MAXCPUFIFO_MASK;
    ip->ip_func[windex] = func;
    ip->ip_arg1[windex] = arg1;
    ip->ip_arg2[windex] = arg2;
    cpu_sfence();
    ++ip->ip_windex;
    --gd->gd_intr_nesting_level;

    /*
     * Do not signal the target cpu, it will pick up the IPI when it next
     * polls (typically on the next tick).
     */
    crit_exit();

    logipiq(send_end, func, arg1, arg2, gd, target);
    return(ip->ip_windex);
}

/*
 * Send an IPI request without blocking, return 0 on success, ENOENT on 
 * failure.  The actual queueing of the hardware IPI may still force us
 * to spin and process incoming IPIs but that will eventually go away
 * when we've gotten rid of the other general IPIs.
 */
int
lwkt_send_ipiq3_nowait(globaldata_t target, ipifunc3_t func, 
		       void *arg1, int arg2)
{
    lwkt_ipiq_t ip;
    int windex;
    struct globaldata *gd = mycpu;

    logipiq(send_nbio, func, arg1, arg2, gd, target);
    KKASSERT(curthread->td_critcount);
    if (target == gd) {
	func(arg1, arg2, NULL);
	logipiq(send_end, func, arg1, arg2, gd, target);
	return(0);
    } 
    ++ipiq_count;
    ip = &gd->gd_ipiq[target->gd_cpuid];

    if (ip->ip_windex - ip->ip_rindex >= MAXCPUFIFO * 2 / 3) {
	logipiq(send_fail, func, arg1, arg2, gd, target);
	return(ENOENT);
    }
    windex = ip->ip_windex & MAXCPUFIFO_MASK;
    ip->ip_func[windex] = func;
    ip->ip_arg1[windex] = arg1;
    ip->ip_arg2[windex] = arg2;
    cpu_sfence();
    ++ip->ip_windex;

    /*
     * This isn't a passive IPI, we still have to signal the target cpu.
     */
    if (atomic_poll_acquire_int(&ip->ip_npoll)) {
	logipiq(cpu_send, func, arg1, arg2, gd, target);
	cpu_send_ipiq(target->gd_cpuid);
    } else {
	if (ipiq_optimized == 0) {
	    logipiq(cpu_send, func, arg1, arg2, gd, target);
	    cpu_send_ipiq(target->gd_cpuid);
	} else {
	    ++ipiq_avoided;
    	}
    }

    logipiq(send_end, func, arg1, arg2, gd, target);
    return(0);
}

/*
 * deprecated, used only by fast int forwarding.
 */
int
lwkt_send_ipiq3_bycpu(int dcpu, ipifunc3_t func, void *arg1, int arg2)
{
    return(lwkt_send_ipiq3(globaldata_find(dcpu), func, arg1, arg2));
}

/*
 * Send a message to several target cpus.  Typically used for scheduling.
 * The message will not be sent to stopped cpus.
 */
int
lwkt_send_ipiq3_mask(u_int32_t mask, ipifunc3_t func, void *arg1, int arg2)
{
    int cpuid;
    int count = 0;

    mask &= ~stopped_cpus;
    while (mask) {
	cpuid = bsfl(mask);
	lwkt_send_ipiq3(globaldata_find(cpuid), func, arg1, arg2);
	mask &= ~(1 << cpuid);
	++count;
    }
    return(count);
}

/*
 * Wait for the remote cpu to finish processing a function.
 *
 * YYY we have to enable interrupts and process the IPIQ while waiting
 * for it to empty or we may deadlock with another cpu.  Create a CPU_*()
 * function to do this!  YYY we really should 'block' here.
 *
 * MUST be called from a critical section.  This routine may be called
 * from an interrupt (for example, if an interrupt wakes a foreign thread
 * up).
 */
void
lwkt_wait_ipiq(globaldata_t target, int seq)
{
    lwkt_ipiq_t ip;
    int maxc = 100000000;

    if (target != mycpu) {
	ip = &mycpu->gd_ipiq[target->gd_cpuid];
	if ((int)(ip->ip_xindex - seq) < 0) {
#if defined(__i386__)
	    unsigned int eflags = read_eflags();
#elif defined(__x86_64__)
	    unsigned long rflags = read_rflags();
#endif
	    cpu_enable_intr();
	    while ((int)(ip->ip_xindex - seq) < 0) {
		crit_enter();
		lwkt_process_ipiq();
		crit_exit();
		if (--maxc == 0)
			kprintf("LWKT_WAIT_IPIQ WARNING! %d wait %d (%d)\n", mycpu->gd_cpuid, target->gd_cpuid, ip->ip_xindex - seq);
		if (maxc < -1000000)
			panic("LWKT_WAIT_IPIQ");
		/*
		 * xindex may be modified by another cpu, use a load fence
		 * to ensure that the loop does not use a speculative value
		 * (which may improve performance).
		 */
		cpu_lfence();
	    }
#if defined(__i386__)
	    write_eflags(eflags);
#elif defined(__x86_64__)
	    write_rflags(rflags);
#endif
	}
    }
}

int
lwkt_seq_ipiq(globaldata_t target)
{
    lwkt_ipiq_t ip;

    ip = &mycpu->gd_ipiq[target->gd_cpuid];
    return(ip->ip_windex);
}

/*
 * Called from IPI interrupt (like a fast interrupt), which has placed
 * us in a critical section.  The MP lock may or may not be held.
 * May also be called from doreti or splz, or be reentrantly called
 * indirectly through the ip_func[] we run.
 *
 * There are two versions, one where no interrupt frame is available (when
 * called from the send code and from splz, and one where an interrupt
 * frame is available.
 */
void
lwkt_process_ipiq(void)
{
    globaldata_t gd = mycpu;
    globaldata_t sgd;
    lwkt_ipiq_t ip;
    int n;

again:
    for (n = 0; n < ncpus; ++n) {
	if (n != gd->gd_cpuid) {
	    sgd = globaldata_find(n);
	    ip = sgd->gd_ipiq;
	    if (ip != NULL) {
		while (lwkt_process_ipiq_core(sgd, &ip[gd->gd_cpuid], NULL))
		    ;
	    }
	}
    }
    if (gd->gd_cpusyncq.ip_rindex != gd->gd_cpusyncq.ip_windex) {
	if (lwkt_process_ipiq_core(gd, &gd->gd_cpusyncq, NULL)) {
	    if (gd->gd_curthread->td_cscount == 0)
		goto again;
	    need_ipiq();
	}
    }
}

void
lwkt_process_ipiq_frame(struct intrframe *frame)
{
    globaldata_t gd = mycpu;
    globaldata_t sgd;
    lwkt_ipiq_t ip;
    int n;

again:
    for (n = 0; n < ncpus; ++n) {
	if (n != gd->gd_cpuid) {
	    sgd = globaldata_find(n);
	    ip = sgd->gd_ipiq;
	    if (ip != NULL) {
		while (lwkt_process_ipiq_core(sgd, &ip[gd->gd_cpuid], frame))
		    ;
	    }
	}
    }
    if (gd->gd_cpusyncq.ip_rindex != gd->gd_cpusyncq.ip_windex) {
	if (lwkt_process_ipiq_core(gd, &gd->gd_cpusyncq, frame)) {
	    if (gd->gd_curthread->td_cscount == 0)
		goto again;
	    need_ipiq();
	}
    }
}

static int
lwkt_process_ipiq_core(globaldata_t sgd, lwkt_ipiq_t ip, 
		       struct intrframe *frame)
{
    globaldata_t mygd = mycpu;
    int ri;
    int wi;
    ipifunc3_t copy_func;
    void *copy_arg1;
    int copy_arg2;

    /*
     * Obtain the current write index, which is modified by a remote cpu.
     * Issue a load fence to prevent speculative reads of e.g. data written
     * by the other cpu prior to it updating the index.
     */
    KKASSERT(curthread->td_critcount);
    wi = ip->ip_windex;
    cpu_lfence();
    ++mygd->gd_intr_nesting_level;

    /*
     * NOTE: xindex is only updated after we are sure the function has
     *	     finished execution.  Beware lwkt_process_ipiq() reentrancy!
     *	     The function may send an IPI which may block/drain.
     *
     * NOTE: Due to additional IPI operations that the callback function
     *	     may make, it is possible for both rindex and windex to advance and
     *	     thus for rindex to advance passed our cached windex.
     *
     * NOTE: A memory fence is required to prevent speculative loads prior
     *	     to the loading of ip_rindex.  Even though stores might be
     *	     ordered, loads are probably not.
     */
    while (wi - (ri = ip->ip_rindex) > 0) {
	ri &= MAXCPUFIFO_MASK;
	cpu_mfence();
	copy_func = ip->ip_func[ri];
	copy_arg1 = ip->ip_arg1[ri];
	copy_arg2 = ip->ip_arg2[ri];
	++ip->ip_rindex;
	KKASSERT((ip->ip_rindex & MAXCPUFIFO_MASK) ==
		 ((ri + 1) & MAXCPUFIFO_MASK));
	logipiq(receive, copy_func, copy_arg1, copy_arg2, sgd, mycpu);
	copy_func(copy_arg1, copy_arg2, frame);
	cpu_sfence();
	ip->ip_xindex = ip->ip_rindex;

#ifdef PANIC_DEBUG
	/*
	 * Simulate panics during the processing of an IPI
	 */
	if (mycpu->gd_cpuid == panic_ipiq_cpu && panic_ipiq_count) {
		if (--panic_ipiq_count == 0) {
#ifdef DDB
			Debugger("PANIC_DEBUG");
#else
			panic("PANIC_DEBUG");
#endif
		}
	}
#endif
    }
    --mygd->gd_intr_nesting_level;

    /*
     * Return non-zero if there are more IPI messages pending on this
     * ipiq.  ip_npoll is left set as long as possible to reduce the
     * number of IPIs queued by the originating cpu, but must be cleared
     * *BEFORE* checking windex.
     */
    atomic_poll_release_int(&ip->ip_npoll);
    return(wi != ip->ip_windex);
}

static void
lwkt_sync_ipiq(void *arg)
{
    cpumask_t *cpumask = arg;

    atomic_clear_int(cpumask, mycpu->gd_cpumask);
    if (*cpumask == 0)
	wakeup(cpumask);
}

void
lwkt_synchronize_ipiqs(const char *wmesg)
{
    cpumask_t other_cpumask;

    other_cpumask = mycpu->gd_other_cpus & smp_active_mask;
    lwkt_send_ipiq_mask(other_cpumask, lwkt_sync_ipiq, &other_cpumask);

    while (other_cpumask != 0) {
	tsleep_interlock(&other_cpumask, 0);
	if (other_cpumask != 0)
	    tsleep(&other_cpumask, PINTERLOCKED, wmesg, 0);
    }
}

#endif

/*
 * CPU Synchronization Support
 *
 * lwkt_cpusync_simple()
 *
 *	The function is executed synchronously before return on remote cpus.
 *	A lwkt_cpusync_t pointer is passed as an argument.  The data can
 *	be accessed via arg->cs_data.
 *
 *	XXX should I just pass the data as an argument to be consistent?
 */

void
lwkt_cpusync_simple(cpumask_t mask, cpusync_func_t func, void *data)
{
    struct lwkt_cpusync cmd;

    cmd.cs_run_func = NULL;
    cmd.cs_fin1_func = func;
    cmd.cs_fin2_func = NULL;
    cmd.cs_data = data;
    lwkt_cpusync_start(mask & mycpu->gd_other_cpus, &cmd);
    if (mask & (1 << mycpu->gd_cpuid))
	func(&cmd);
    lwkt_cpusync_finish(&cmd);
}

/*
 * lwkt_cpusync_fastdata()
 *
 *	The function is executed in tandem with return on remote cpus.
 *	The data is directly passed as an argument.  Do not pass pointers to
 *	temporary storage as the storage might have
 *	gone poof by the time the target cpu executes
 *	the function.
 *
 *	At the moment lwkt_cpusync is declared on the stack and we must wait
 *	for all remote cpus to ack in lwkt_cpusync_finish(), but as a future
 *	optimization we should be able to put a counter in the globaldata
 *	structure (if it is not otherwise being used) and just poke it and
 *	return without waiting. XXX
 */
void
lwkt_cpusync_fastdata(cpumask_t mask, cpusync_func2_t func, void *data)
{
    struct lwkt_cpusync cmd;

    cmd.cs_run_func = NULL;
    cmd.cs_fin1_func = NULL;
    cmd.cs_fin2_func = func;
    cmd.cs_data = NULL;
    lwkt_cpusync_start(mask & mycpu->gd_other_cpus, &cmd);
    if (mask & (1 << mycpu->gd_cpuid))
	func(data);
    lwkt_cpusync_finish(&cmd);
}

/*
 * lwkt_cpusync_start()
 *
 *	Start synchronization with a set of target cpus, return once they are
 *	known to be in a synchronization loop.  The target cpus will execute
 *	poll->cs_run_func() IN TANDEM WITH THE RETURN.
 *
 *	XXX future: add lwkt_cpusync_start_quick() and require a call to
 *	lwkt_cpusync_add() or lwkt_cpusync_wait(), allowing the caller to
 *	potentially absorb the IPI latency doing something useful.
 */
void
lwkt_cpusync_start(cpumask_t mask, lwkt_cpusync_t poll)
{
    globaldata_t gd = mycpu;

    poll->cs_count = 0;
    poll->cs_mask = mask;
#ifdef SMP
    logipiq2(sync_start, mask & gd->gd_other_cpus);
    poll->cs_maxcount = lwkt_send_ipiq_mask(
		mask & gd->gd_other_cpus & smp_active_mask,
		(ipifunc1_t)lwkt_cpusync_remote1, poll);
#endif
    if (mask & gd->gd_cpumask) {
	if (poll->cs_run_func)
	    poll->cs_run_func(poll);
    }
#ifdef SMP
    if (poll->cs_maxcount) {
	++ipiq_cscount;
	++gd->gd_curthread->td_cscount;
	while (poll->cs_count != poll->cs_maxcount) {
	    crit_enter();
	    lwkt_process_ipiq();
	    crit_exit();
	}
    }
#endif
}

void
lwkt_cpusync_add(cpumask_t mask, lwkt_cpusync_t poll)
{
    globaldata_t gd = mycpu;
#ifdef SMP
    int count;
#endif

    mask &= ~poll->cs_mask;
    poll->cs_mask |= mask;
#ifdef SMP
    logipiq2(sync_add, mask & gd->gd_other_cpus);
    count = lwkt_send_ipiq_mask(
		mask & gd->gd_other_cpus & smp_active_mask,
		(ipifunc1_t)lwkt_cpusync_remote1, poll);
#endif
    if (mask & gd->gd_cpumask) {
	if (poll->cs_run_func)
	    poll->cs_run_func(poll);
    }
#ifdef SMP
    poll->cs_maxcount += count;
    if (poll->cs_maxcount) {
	if (poll->cs_maxcount == count)
	    ++gd->gd_curthread->td_cscount;
	while (poll->cs_count != poll->cs_maxcount) {
	    crit_enter();
	    lwkt_process_ipiq();
	    crit_exit();
	}
    }
#endif
}

/*
 * Finish synchronization with a set of target cpus.  The target cpus will
 * execute cs_fin1_func(poll) prior to this function returning, and will
 * execute cs_fin2_func(data) IN TANDEM WITH THIS FUNCTION'S RETURN.
 *
 * If cs_maxcount is non-zero then we are mastering a cpusync with one or
 * more remote cpus and must account for it in our thread structure.
 */
void
lwkt_cpusync_finish(lwkt_cpusync_t poll)
{
    globaldata_t gd = mycpu;

    poll->cs_count = -1;
    if (poll->cs_mask & gd->gd_cpumask) {
	if (poll->cs_fin1_func)
	    poll->cs_fin1_func(poll);
	if (poll->cs_fin2_func)
	    poll->cs_fin2_func(poll->cs_data);
    }
#ifdef SMP
    if (poll->cs_maxcount) {
	while (poll->cs_count != -(poll->cs_maxcount + 1)) {
	    crit_enter();
	    lwkt_process_ipiq();
	    crit_exit();
	}
	--gd->gd_curthread->td_cscount;
    }
#endif
}

#ifdef SMP

/*
 * helper IPI remote messaging function.
 * 
 * Called on remote cpu when a new cpu synchronization request has been
 * sent to us.  Execute the run function and adjust cs_count, then requeue
 * the request so we spin on it.
 */
static void
lwkt_cpusync_remote1(lwkt_cpusync_t poll)
{
    atomic_add_int(&poll->cs_count, 1);
    if (poll->cs_run_func)
	poll->cs_run_func(poll);
    lwkt_cpusync_remote2(poll);
}

/*
 * helper IPI remote messaging function.
 *
 * Poll for the originator telling us to finish.  If it hasn't, requeue
 * our request so we spin on it.  When the originator requests that we
 * finish we execute cs_fin1_func(poll) synchronously and cs_fin2_func(data)
 * in tandem with the release.
 */
static void
lwkt_cpusync_remote2(lwkt_cpusync_t poll)
{
    if (poll->cs_count < 0) {
	cpusync_func2_t savef;
	void *saved;

	if (poll->cs_fin1_func)
	    poll->cs_fin1_func(poll);
	if (poll->cs_fin2_func) {
	    savef = poll->cs_fin2_func;
	    saved = poll->cs_data;
	    atomic_add_int(&poll->cs_count, -1);
	    savef(saved);
	} else {
	    atomic_add_int(&poll->cs_count, -1);
	}
    } else {
	globaldata_t gd = mycpu;
	lwkt_ipiq_t ip;
	int wi;

	ip = &gd->gd_cpusyncq;
	wi = ip->ip_windex & MAXCPUFIFO_MASK;
	ip->ip_func[wi] = (ipifunc3_t)(ipifunc1_t)lwkt_cpusync_remote2;
	ip->ip_arg1[wi] = poll;
	ip->ip_arg2[wi] = 0;
	cpu_sfence();
	++ip->ip_windex;
    }
}

#endif
