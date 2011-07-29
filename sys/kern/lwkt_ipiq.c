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
static int ipiq_debug;		/* set to 1 for debug */
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
SYSCTL_INT(_lwkt, OID_AUTO, ipiq_debug, CTLFLAG_RW, &ipiq_debug, 0,
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
KTR_INFO(KTR_IPIQ, ipiq, sync_end, 6, "cpumask=%08x", sizeof(cpumask_t));
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
static void lwkt_cpusync_remote1(lwkt_cpusync_t cs);
static void lwkt_cpusync_remote2(lwkt_cpusync_t cs);

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
     *
     * The target ipiq may have gotten filled up due to passive IPIs and thus
     * not be aware that its queue is too full, so be sure to issue an
     * ipiq interrupt to the target cpu.
     */
    if (ip->ip_windex - ip->ip_rindex > MAXCPUFIFO / 2) {
#if defined(__i386__)
	unsigned int eflags = read_eflags();
#elif defined(__x86_64__)
	unsigned long rflags = read_rflags();
#endif

	cpu_enable_intr();
	++ipiq_fifofull;
	DEBUG_PUSH_INFO("send_ipiq3");
	while (ip->ip_windex - ip->ip_rindex > MAXCPUFIFO / 4) {
	    if (atomic_poll_acquire_int(&ip->ip_npoll) || ipiq_optimized == 0) {
		logipiq(cpu_send, func, arg1, arg2, gd, target);
		cpu_send_ipiq(target->gd_cpuid);
	    }
	    KKASSERT(ip->ip_windex - ip->ip_rindex != MAXCPUFIFO - 1);
	    lwkt_process_ipiq();
	    cpu_pause();
	}
	DEBUG_POP_INFO();
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

    /*
     * signal the target cpu that there is work pending.
     */
    if (atomic_poll_acquire_int(&ip->ip_npoll) || ipiq_optimized == 0) {
	logipiq(cpu_send, func, arg1, arg2, gd, target);
	cpu_send_ipiq(target->gd_cpuid);
    } else {
	++ipiq_avoided;
    }
    --gd->gd_intr_nesting_level;
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
    ++gd->gd_intr_nesting_level;
    logipiq(send_pasv, func, arg1, arg2, gd, target);
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

	cpu_enable_intr();
	++ipiq_fifofull;
	DEBUG_PUSH_INFO("send_ipiq3_passive");
	while (ip->ip_windex - ip->ip_rindex > MAXCPUFIFO / 4) {
	    if (atomic_poll_acquire_int(&ip->ip_npoll) || ipiq_optimized == 0) {
		logipiq(cpu_send, func, arg1, arg2, gd, target);
		cpu_send_ipiq(target->gd_cpuid);
	    }
	    KKASSERT(ip->ip_windex - ip->ip_rindex != MAXCPUFIFO - 1);
	    lwkt_process_ipiq();
	    cpu_pause();
	}
	DEBUG_POP_INFO();
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
    crit_enter();
    ++gd->gd_intr_nesting_level;
    ++ipiq_count;
    ip = &gd->gd_ipiq[target->gd_cpuid];

    if (ip->ip_windex - ip->ip_rindex >= MAXCPUFIFO * 2 / 3) {
	logipiq(send_fail, func, arg1, arg2, gd, target);
	--gd->gd_intr_nesting_level;
	crit_exit();
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
    if (atomic_poll_acquire_int(&ip->ip_npoll) || ipiq_optimized == 0) {
	logipiq(cpu_send, func, arg1, arg2, gd, target);
	cpu_send_ipiq(target->gd_cpuid);
    } else {
	++ipiq_avoided;
    }
    --gd->gd_intr_nesting_level;
    crit_exit();

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
lwkt_send_ipiq3_mask(cpumask_t mask, ipifunc3_t func, void *arg1, int arg2)
{
    int cpuid;
    int count = 0;

    mask &= ~stopped_cpus;
    while (mask) {
	cpuid = BSFCPUMASK(mask);
	lwkt_send_ipiq3(globaldata_find(cpuid), func, arg1, arg2);
	mask &= ~CPUMASK(cpuid);
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
	    DEBUG_PUSH_INFO("wait_ipiq");
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
	    DEBUG_POP_INFO();
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
 *
 * When the current cpu is mastering a cpusync we do NOT internally loop
 * on the cpusyncq poll.  We also do not re-flag a pending ipi due to
 * the cpusyncq poll because this can cause doreti/splz to loop internally.
 * The cpusync master's own loop must be allowed to run to avoid a deadlock.
 */
void
lwkt_process_ipiq(void)
{
    globaldata_t gd = mycpu;
    globaldata_t sgd;
    lwkt_ipiq_t ip;
    int n;

    ++gd->gd_processing_ipiq;
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
    if (lwkt_process_ipiq_core(gd, &gd->gd_cpusyncq, NULL)) {
	if (gd->gd_curthread->td_cscount == 0)
	    goto again;
	/* need_ipiq(); do not reflag */
    }
    --gd->gd_processing_ipiq;
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
	    /* need_ipiq(); do not reflag */
	}
    }
}

#if 0
static int iqticks[SMP_MAXCPU];
static int iqcount[SMP_MAXCPU];
#endif
#if 0
static int iqterm[SMP_MAXCPU];
#endif

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

#if 0
    if (iqticks[mygd->gd_cpuid] != ticks) {
	    iqticks[mygd->gd_cpuid] = ticks;
	    iqcount[mygd->gd_cpuid] = 0;
    }
    if (++iqcount[mygd->gd_cpuid] > 3000000) {
	kprintf("cpu %d ipiq maxed cscount %d spin %d\n",
		mygd->gd_cpuid,
		mygd->gd_curthread->td_cscount,
		mygd->gd_spinlocks_wr);
	iqcount[mygd->gd_cpuid] = 0;
#if 0
	if (++iqterm[mygd->gd_cpuid] > 10)
		panic("cpu %d ipiq maxed", mygd->gd_cpuid);
#endif
	int i;
	for (i = 0; i < ncpus; ++i) {
		if (globaldata_find(i)->gd_infomsg)
			kprintf(" %s", globaldata_find(i)->gd_infomsg);
	}
	kprintf("\n");
    }
#endif

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
     * NOTE: A load fence is required to prevent speculative loads prior
     *	     to the loading of ip_rindex.  Even though stores might be
     *	     ordered, loads are probably not.  A memory fence is required
     *	     to prevent reordering of the loads after the ip_rindex update.
     */
    while (wi - (ri = ip->ip_rindex) > 0) {
	ri &= MAXCPUFIFO_MASK;
	cpu_lfence();
	copy_func = ip->ip_func[ri];
	copy_arg1 = ip->ip_arg1[ri];
	copy_arg2 = ip->ip_arg2[ri];
	cpu_mfence();
	++ip->ip_rindex;
	KKASSERT((ip->ip_rindex & MAXCPUFIFO_MASK) ==
		 ((ri + 1) & MAXCPUFIFO_MASK));
	logipiq(receive, copy_func, copy_arg1, copy_arg2, sgd, mycpu);
#ifdef INVARIANTS
	if (ipiq_debug && (ip->ip_rindex & 0xFFFFFF) == 0) {
		kprintf("cpu %d ipifunc %p %p %d (frame %p)\n",
			mycpu->gd_cpuid,
			copy_func, copy_arg1, copy_arg2,
#if defined(__i386__)
			(frame ? (void *)frame->if_eip : NULL));
#elif defined(__amd64__)
			(frame ? (void *)frame->if_rip : NULL));
#else
			NULL);
#endif
	}
#endif
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
     * If the queue is empty release ip_npoll to enable the other cpu to
     * send us an IPI interrupt again.
     *
     * Return non-zero if there is still more in the queue.  Note that we
     * must re-check the indexes after potentially releasing ip_npoll.  The
     * caller must loop or otherwise ensure that a loop will occur prior to
     * blocking.
     */
    if (ip->ip_rindex == ip->ip_windex);
	    atomic_poll_release_int(&ip->ip_npoll);
    cpu_lfence();
    return (ip->ip_rindex != ip->ip_windex);
}

static void
lwkt_sync_ipiq(void *arg)
{
    volatile cpumask_t *cpumask = arg;

    atomic_clear_cpumask(cpumask, mycpu->gd_cpumask);
    if (*cpumask == 0)
	wakeup(cpumask);
}

void
lwkt_synchronize_ipiqs(const char *wmesg)
{
    volatile cpumask_t other_cpumask;

    other_cpumask = mycpu->gd_other_cpus & smp_active_mask;
    lwkt_send_ipiq_mask(other_cpumask, lwkt_sync_ipiq,
    	__DEVOLATILE(void *, &other_cpumask));

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
 * lwkt_cpusync_interlock()	- Place specified cpus in a quiescent state.
 *				  The current cpu is placed in a hard critical
 *				  section.
 *
 * lwkt_cpusync_deinterlock()	- Execute cs_func on specified cpus, including
 *				  current cpu if specified, then return.
 */
void
lwkt_cpusync_simple(cpumask_t mask, cpusync_func_t func, void *arg)
{
    struct lwkt_cpusync cs;

    lwkt_cpusync_init(&cs, mask, func, arg);
    lwkt_cpusync_interlock(&cs);
    lwkt_cpusync_deinterlock(&cs);
}


void
lwkt_cpusync_interlock(lwkt_cpusync_t cs)
{
#ifdef SMP
    globaldata_t gd = mycpu;
    cpumask_t mask;

    /*
     * mask acknowledge (cs_mack):  0->mask for stage 1
     *
     * mack does not include the current cpu.
     */
    mask = cs->cs_mask & gd->gd_other_cpus & smp_active_mask;
    cs->cs_mack = 0;
    crit_enter_id("cpusync");
    if (mask) {
	DEBUG_PUSH_INFO("cpusync_interlock");
	++ipiq_cscount;
	++gd->gd_curthread->td_cscount;
	lwkt_send_ipiq_mask(mask, (ipifunc1_t)lwkt_cpusync_remote1, cs);
	logipiq2(sync_start, mask);
	while (cs->cs_mack != mask) {
	    lwkt_process_ipiq();
	    cpu_pause();
	}
	DEBUG_POP_INFO();
    }
#else
    cs->cs_mack = 0;
#endif
}

/*
 * Interlocked cpus have executed remote1 and are polling in remote2.
 * To deinterlock we clear cs_mack and wait for the cpus to execute
 * the func and set their bit in cs_mack again.
 *
 */
void
lwkt_cpusync_deinterlock(lwkt_cpusync_t cs)
{
    globaldata_t gd = mycpu;
#ifdef SMP
    cpumask_t mask;

    /*
     * mask acknowledge (cs_mack):  mack->0->mack for stage 2
     *
     * Clearing cpu bits for polling cpus in cs_mack will cause them to
     * execute stage 2, which executes the cs_func(cs_data) and then sets
     * their bit in cs_mack again.
     *
     * mack does not include the current cpu.
     */
    mask = cs->cs_mack;
    cpu_ccfence();
    cs->cs_mack = 0;
    if (cs->cs_func && (cs->cs_mask & gd->gd_cpumask))
	    cs->cs_func(cs->cs_data);
    if (mask) {
	DEBUG_PUSH_INFO("cpusync_deinterlock");
	while (cs->cs_mack != mask) {
	    lwkt_process_ipiq();
	    cpu_pause();
	}
	DEBUG_POP_INFO();
	/*
	 * cpusyncq ipis may be left queued without the RQF flag set due to
	 * a non-zero td_cscount, so be sure to process any laggards after
	 * decrementing td_cscount.
	 */
	--gd->gd_curthread->td_cscount;
	lwkt_process_ipiq();
	logipiq2(sync_end, mask);
    }
    crit_exit_id("cpusync");
#else
    if (cs->cs_func && (cs->cs_mask & gd->gd_cpumask))
	cs->cs_func(cs->cs_data);
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
lwkt_cpusync_remote1(lwkt_cpusync_t cs)
{
    globaldata_t gd = mycpu;

    atomic_set_cpumask(&cs->cs_mack, gd->gd_cpumask);
    lwkt_cpusync_remote2(cs);
}

/*
 * helper IPI remote messaging function.
 *
 * Poll for the originator telling us to finish.  If it hasn't, requeue
 * our request so we spin on it.
 */
static void
lwkt_cpusync_remote2(lwkt_cpusync_t cs)
{
    globaldata_t gd = mycpu;

    if ((cs->cs_mack & gd->gd_cpumask) == 0) {
	if (cs->cs_func)
		cs->cs_func(cs->cs_data);
	atomic_set_cpumask(&cs->cs_mack, gd->gd_cpumask);
    } else {
	lwkt_ipiq_t ip;
	int wi;

	ip = &gd->gd_cpusyncq;
	wi = ip->ip_windex & MAXCPUFIFO_MASK;
	ip->ip_func[wi] = (ipifunc3_t)(ipifunc1_t)lwkt_cpusync_remote2;
	ip->ip_arg1[wi] = cs;
	ip->ip_arg2[wi] = 0;
	cpu_sfence();
	++ip->ip_windex;
	if (ipiq_debug && (ip->ip_windex & 0xFFFFFF) == 0) {
		kprintf("cpu %d cm=%016jx %016jx f=%p\n",
			gd->gd_cpuid,
			(intmax_t)cs->cs_mask, (intmax_t)cs->cs_mack,
			cs->cs_func);
	}
    }
}

#endif
