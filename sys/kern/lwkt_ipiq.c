/*
 * Copyright (c) 2003-2016 The DragonFly Project.  All rights reserved.
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
#include <machine/clock.h>
#include <machine/atomic.h>

#ifdef _KERNEL_VIRTUAL
#include <pthread.h>
#endif

struct ipiq_stats {
    int64_t ipiq_count;		/* total calls to lwkt_send_ipiq*() */
    int64_t ipiq_fifofull;	/* number of fifo full conditions detected */
    int64_t ipiq_avoided;	/* interlock with target avoids cpu ipi */
    int64_t ipiq_passive;	/* passive IPI messages */
    int64_t ipiq_cscount;	/* number of cpu synchronizations */
} __cachealign;

static struct ipiq_stats ipiq_stats_percpu[MAXCPU];
#define ipiq_stat(gd)	ipiq_stats_percpu[(gd)->gd_cpuid]

static int ipiq_debug;		/* set to 1 for debug */
#ifdef PANIC_DEBUG
static int	panic_ipiq_cpu = -1;
static int	panic_ipiq_count = 100;
#endif

SYSCTL_INT(_lwkt, OID_AUTO, ipiq_debug, CTLFLAG_RW, &ipiq_debug, 0,
    "");
#ifdef PANIC_DEBUG
SYSCTL_INT(_lwkt, OID_AUTO, panic_ipiq_cpu, CTLFLAG_RW, &panic_ipiq_cpu, 0, "");
SYSCTL_INT(_lwkt, OID_AUTO, panic_ipiq_count, CTLFLAG_RW, &panic_ipiq_count, 0, "");
#endif

#define IPIQ_STRING	"func=%p arg1=%p arg2=%d scpu=%d dcpu=%d"
#define IPIQ_ARGS	void *func, void *arg1, int arg2, int scpu, int dcpu

#if !defined(KTR_IPIQ)
#define KTR_IPIQ	KTR_ALL
#endif
KTR_INFO_MASTER(ipiq);
KTR_INFO(KTR_IPIQ, ipiq, send_norm, 0, IPIQ_STRING, IPIQ_ARGS);
KTR_INFO(KTR_IPIQ, ipiq, send_pasv, 1, IPIQ_STRING, IPIQ_ARGS);
KTR_INFO(KTR_IPIQ, ipiq, receive, 4, IPIQ_STRING, IPIQ_ARGS);
KTR_INFO(KTR_IPIQ, ipiq, sync_start, 5, "cpumask=%08lx", unsigned long mask);
KTR_INFO(KTR_IPIQ, ipiq, sync_end, 6, "cpumask=%08lx", unsigned long mask);
KTR_INFO(KTR_IPIQ, ipiq, cpu_send, 7, IPIQ_STRING, IPIQ_ARGS);
KTR_INFO(KTR_IPIQ, ipiq, send_end, 8, IPIQ_STRING, IPIQ_ARGS);
KTR_INFO(KTR_IPIQ, ipiq, sync_quick, 9, "cpumask=%08lx", unsigned long mask);

#define logipiq(name, func, arg1, arg2, sgd, dgd)	\
	KTR_LOG(ipiq_ ## name, func, arg1, arg2, sgd->gd_cpuid, dgd->gd_cpuid)
#define logipiq2(name, arg)	\
	KTR_LOG(ipiq_ ## name, arg)

static void lwkt_process_ipiq_nested(void);
static int lwkt_process_ipiq_core(globaldata_t sgd, lwkt_ipiq_t ip, 
				  struct intrframe *frame, int limit);
static void lwkt_cpusync_remote1(lwkt_cpusync_t cs);
static void lwkt_cpusync_remote2(lwkt_cpusync_t cs);

#define IPIQ_SYSCTL(name)				\
static int						\
sysctl_##name(SYSCTL_HANDLER_ARGS)			\
{							\
    int64_t val = 0;					\
    int cpu, error;					\
							\
    for (cpu = 0; cpu < ncpus; ++cpu)			\
	val += ipiq_stats_percpu[cpu].name;		\
							\
    error = sysctl_handle_quad(oidp, &val, 0, req);	\
    if (error || req->newptr == NULL)			\
	return error;					\
							\
    for (cpu = 0; cpu < ncpus; ++cpu)			\
    	ipiq_stats_percpu[cpu].name = val;		\
							\
    return 0;						\
}

IPIQ_SYSCTL(ipiq_count);
IPIQ_SYSCTL(ipiq_fifofull);
IPIQ_SYSCTL(ipiq_avoided);
IPIQ_SYSCTL(ipiq_passive);
IPIQ_SYSCTL(ipiq_cscount);

SYSCTL_PROC(_lwkt, OID_AUTO, ipiq_count, (CTLTYPE_QUAD | CTLFLAG_RW),
    0, 0, sysctl_ipiq_count, "Q", "Number of IPI's sent");
SYSCTL_PROC(_lwkt, OID_AUTO, ipiq_fifofull, (CTLTYPE_QUAD | CTLFLAG_RW),
    0, 0, sysctl_ipiq_fifofull, "Q",
    "Number of fifo full conditions detected");
SYSCTL_PROC(_lwkt, OID_AUTO, ipiq_avoided, (CTLTYPE_QUAD | CTLFLAG_RW),
    0, 0, sysctl_ipiq_avoided, "Q",
    "Number of IPI's avoided by interlock with target cpu");
SYSCTL_PROC(_lwkt, OID_AUTO, ipiq_passive, (CTLTYPE_QUAD | CTLFLAG_RW),
    0, 0, sysctl_ipiq_passive, "Q",
    "Number of passive IPI messages sent");
SYSCTL_PROC(_lwkt, OID_AUTO, ipiq_cscount, (CTLTYPE_QUAD | CTLFLAG_RW),
    0, 0, sysctl_ipiq_cscount, "Q",
    "Number of cpu synchronizations");

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
    int level1;
    int level2;
    long rflags;
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
    ++ipiq_stat(gd).ipiq_count;
    ip = &gd->gd_ipiq[target->gd_cpuid];

    /*
     * Do not allow the FIFO to become full.  Interrupts must be physically
     * enabled while we liveloop to avoid deadlocking the APIC.
     *
     * When we are not nested inside a processing loop we allow the FIFO
     * to get 1/2 full.  Once it exceeds 1/2 full we must wait for it to
     * drain, executing any incoming IPIs while we wait.
     *
     * When we are nested we allow the FIFO to get almost completely full.
     * This allows us to queue IPIs sent from IPI callbacks.  The processing
     * code will only process incoming FIFOs that are trying to drain while
     * we wait, and only to the only-slightly-less-full point, to avoid a
     * deadlock.
     *
     * We are guaranteed
     */

    if (gd->gd_processing_ipiq == 0) {
	level1 = MAXCPUFIFO / 2;
	level2 = MAXCPUFIFO / 4;
    } else {
	level1 = MAXCPUFIFO - 3;
	level2 = MAXCPUFIFO - 5;
    }

    if (ip->ip_windex - ip->ip_rindex > level1) {
#ifndef _KERNEL_VIRTUAL
	uint64_t tsc_base = rdtsc();
#endif
	int repeating = 0;
	int olimit;

	rflags = read_rflags();
	cpu_enable_intr();
	++ipiq_stat(gd).ipiq_fifofull;
	DEBUG_PUSH_INFO("send_ipiq3");
	olimit = atomic_swap_int(&ip->ip_drain, level2);
	while (ip->ip_windex - ip->ip_rindex > level2) {
	    KKASSERT(ip->ip_windex - ip->ip_rindex != MAXCPUFIFO - 1);
	    lwkt_process_ipiq_nested();
	    cpu_pause();

	    /*
	     * Check for target not draining issue.  This should be fixed but
	     * leave the code in-place anyway as it can recover an otherwise
	     * dead system.
	     */
#ifdef _KERNEL_VIRTUAL
	    if (repeating++ > 10)
		    pthread_yield();
#else
	    if (rdtsc() - tsc_base > tsc_frequency) {
		++repeating;
		if (repeating > 10) {
			kprintf("send_ipiq %d->%d tgt not draining (%d) sniff=%p,%p\n",
				gd->gd_cpuid, target->gd_cpuid, repeating,
				target->gd_sample_pc, target->gd_sample_sp);
			smp_sniff();
			ATOMIC_CPUMASK_ORBIT(target->gd_ipimask, gd->gd_cpuid);
			cpu_send_ipiq(target->gd_cpuid);
		} else {
			kprintf("send_ipiq %d->%d tgt not draining (%d)\n",
				gd->gd_cpuid, target->gd_cpuid, repeating);
			smp_sniff();
		}
		tsc_base = rdtsc();
	    }
#endif
	}
	atomic_swap_int(&ip->ip_drain, olimit);
	DEBUG_POP_INFO();
#if defined(__x86_64__)
	write_rflags(rflags);
#else
#error "no write_*flags"
#endif
    }

    /*
     * Queue the new message and signal the target cpu.  For now we need to
     * physically disable interrupts because the target will not get signalled
     * by other cpus once we set target->gd_npoll and we don't want to get
     * interrupted.
     *
     * XXX not sure why this is a problem, the critical section should prevent
     *     any stalls (incoming interrupts except Xinvltlb and Xsnoop will
     *	   just be made pending).
     */
    rflags = read_rflags();
#ifndef _KERNEL_VIRTUAL
    cpu_disable_intr();
#endif

    windex = ip->ip_windex & MAXCPUFIFO_MASK;
    ip->ip_info[windex].func = func;
    ip->ip_info[windex].arg1 = arg1;
    ip->ip_info[windex].arg2 = arg2;
    cpu_sfence();
    ++ip->ip_windex;
    ATOMIC_CPUMASK_ORBIT(target->gd_ipimask, gd->gd_cpuid);

    /*
     * signal the target cpu that there is work pending.
     */
    if (atomic_swap_int(&target->gd_npoll, 1) == 0) {
	logipiq(cpu_send, func, arg1, arg2, gd, target);
	cpu_send_ipiq(target->gd_cpuid);
    } else {
	++ipiq_stat(gd).ipiq_avoided;
    }
    write_rflags(rflags);

    --gd->gd_intr_nesting_level;
    crit_exit();
    logipiq(send_end, func, arg1, arg2, gd, target);

    return(ip->ip_windex);
}

/*
 * Similar to lwkt_send_ipiq() but this function does not actually initiate
 * the IPI to the target cpu unless the FIFO is greater than 1/4 full.
 * This function is usually very fast.
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
    crit_enter_gd(gd);
    ++gd->gd_intr_nesting_level;
    ip = &gd->gd_ipiq[target->gd_cpuid];

    /*
     * If the FIFO is too full send the IPI actively.
     *
     * WARNING! This level must be low enough not to trigger a wait loop
     *		in the active sending code since we are not signalling the
     *		target cpu.
     */
    if (ip->ip_windex - ip->ip_rindex >= MAXCPUFIFO / 4) {
	--gd->gd_intr_nesting_level;
	crit_exit_gd(gd);
	return lwkt_send_ipiq3(target, func, arg1, arg2);
    }

    /*
     * Else we can do it passively.
     */
    logipiq(send_pasv, func, arg1, arg2, gd, target);
    ++ipiq_stat(gd).ipiq_count;
    ++ipiq_stat(gd).ipiq_passive;

    /*
     * Queue the new message
     */
    windex = ip->ip_windex & MAXCPUFIFO_MASK;
    ip->ip_info[windex].func = func;
    ip->ip_info[windex].arg1 = arg1;
    ip->ip_info[windex].arg2 = arg2;
    cpu_sfence();
    ++ip->ip_windex;
    ATOMIC_CPUMASK_ORBIT(target->gd_ipimask, gd->gd_cpuid);
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
 *
 * To prevent treating low-numbered cpus as favored sons, the IPIs are
 * issued in order starting at mycpu upward, then from 0 through mycpu.
 * This is particularly important to prevent random scheduler pickups
 * from favoring cpu 0.
 */
int
lwkt_send_ipiq3_mask(cpumask_t mask, ipifunc3_t func, void *arg1, int arg2)
{
    int cpuid;
    int count = 0;
    cpumask_t amask;

    CPUMASK_NANDMASK(mask, stopped_cpus);

    /*
     * All cpus in mask which are >= mycpu
     */
    CPUMASK_ASSBMASK(amask, mycpu->gd_cpuid);
    CPUMASK_INVMASK(amask);
    CPUMASK_ANDMASK(amask, mask);
    while (CPUMASK_TESTNZERO(amask)) {
	cpuid = BSFCPUMASK(amask);
	lwkt_send_ipiq3(globaldata_find(cpuid), func, arg1, arg2);
	CPUMASK_NANDBIT(amask, cpuid);
	++count;
    }

    /*
     * All cpus in mask which are < mycpu
     */
    CPUMASK_ASSBMASK(amask, mycpu->gd_cpuid);
    CPUMASK_ANDMASK(amask, mask);
    while (CPUMASK_TESTNZERO(amask)) {
	cpuid = BSFCPUMASK(amask);
	lwkt_send_ipiq3(globaldata_find(cpuid), func, arg1, arg2);
	CPUMASK_NANDBIT(amask, cpuid);
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

    if (target != mycpu) {
	ip = &mycpu->gd_ipiq[target->gd_cpuid];
	if ((int)(ip->ip_xindex - seq) < 0) {
#if defined(__x86_64__)
	    unsigned long rflags = read_rflags();
#else
#error "no read_*flags"
#endif
	    int64_t time_tgt = tsc_get_target(1000000000LL);
	    int time_loops = 10;
	    int benice = 0;
#ifdef _KERNEL_VIRTUAL
	    int repeating = 0;
#endif

	    cpu_enable_intr();
	    DEBUG_PUSH_INFO("wait_ipiq");
	    while ((int)(ip->ip_xindex - seq) < 0) {
		crit_enter();
		lwkt_process_ipiq();
		crit_exit();
#ifdef _KERNEL_VIRTUAL
		if (repeating++ > 10)
			pthread_yield();
#endif

		/*
		 * IPIQs must be handled within 10 seconds and this code
		 * will warn after one second.
		 */
		if ((benice & 255) == 0 && tsc_test_target(time_tgt) > 0) {
			kprintf("LWKT_WAIT_IPIQ WARNING! %d wait %d (%d)\n",
				mycpu->gd_cpuid, target->gd_cpuid,
				ip->ip_xindex - seq);
			if (--time_loops == 0)
				panic("LWKT_WAIT_IPIQ");
			time_tgt = tsc_get_target(1000000000LL);
		}
		++benice;

		/*
		 * xindex may be modified by another cpu, use a load fence
		 * to ensure that the loop does not use a speculative value
		 * (which may improve performance).
		 */
		cpu_pause();
		cpu_lfence();
	    }
	    DEBUG_POP_INFO();
#if defined(__x86_64__)
	    write_rflags(rflags);
#else
#error "no write_*flags"
#endif
	}
    }
}

/*
 * Called from IPI interrupt (like a fast interrupt), which has placed
 * us in a critical section.  The MP lock may or may not be held.
 * May also be called from doreti or splz, or be reentrantly called
 * indirectly through the ip_info[].func we run.
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
    cpumask_t mask;
    int n;

    ++gd->gd_processing_ipiq;
again:
    mask = gd->gd_ipimask;
    cpu_ccfence();
    while (CPUMASK_TESTNZERO(mask)) {
	n = BSFCPUMASK(mask);
	if (n != gd->gd_cpuid) {
	    sgd = globaldata_find(n);
	    ip = sgd->gd_ipiq;
	    if (ip != NULL) {
		ip += gd->gd_cpuid;
		while (lwkt_process_ipiq_core(sgd, ip, NULL, 0))
		    ;
		ATOMIC_CPUMASK_NANDBIT(gd->gd_ipimask, n);
		if (ip->ip_rindex != ip->ip_windex)
			ATOMIC_CPUMASK_ORBIT(gd->gd_ipimask, n);
	    }
	}
	CPUMASK_NANDBIT(mask, n);
    }

    /*
     * Process pending cpusyncs.  If the current thread has a cpusync
     * active cpusync we only run the list once and do not re-flag
     * as the thread itself is processing its interlock.
     */
    if (lwkt_process_ipiq_core(gd, &gd->gd_cpusyncq, NULL, 0)) {
	if (gd->gd_curthread->td_cscount == 0)
	    goto again;
	/* need_ipiq(); do not reflag */
    }

    /*
     * Interlock to allow more IPI interrupts.
     */
    --gd->gd_processing_ipiq;
}

void
lwkt_process_ipiq_frame(struct intrframe *frame)
{
    globaldata_t gd = mycpu;
    globaldata_t sgd;
    lwkt_ipiq_t ip;
    cpumask_t mask;
    int n;

    ++gd->gd_processing_ipiq;
again:
    mask = gd->gd_ipimask;
    cpu_ccfence();
    while (CPUMASK_TESTNZERO(mask)) {
	n = BSFCPUMASK(mask);
	if (n != gd->gd_cpuid) {
	    sgd = globaldata_find(n);
	    ip = sgd->gd_ipiq;
	    if (ip != NULL) {
		ip += gd->gd_cpuid;
		while (lwkt_process_ipiq_core(sgd, ip, frame, 0))
		    ;
		ATOMIC_CPUMASK_NANDBIT(gd->gd_ipimask, n);
		if (ip->ip_rindex != ip->ip_windex)
			ATOMIC_CPUMASK_ORBIT(gd->gd_ipimask, n);
	    }
	}
	CPUMASK_NANDBIT(mask, n);
    }
    if (gd->gd_cpusyncq.ip_rindex != gd->gd_cpusyncq.ip_windex) {
	if (lwkt_process_ipiq_core(gd, &gd->gd_cpusyncq, frame, 0)) {
	    if (gd->gd_curthread->td_cscount == 0)
		goto again;
	    /* need_ipiq(); do not reflag */
	}
    }
    --gd->gd_processing_ipiq;
}

/*
 * Only process incoming IPIQs from draining senders and only process them
 * to the point where the draining sender is able to continue.  This is
 * necessary to avoid deadlocking the IPI subsystem because we are acting on
 * incoming messages and the callback may queue additional messages.
 *
 * We only want to have to act on senders that are blocked to limit the
 * number of additional messages sent.  At the same time, recipients are
 * trying to drain our own queue.  Theoretically this create a pipeline that
 * cannot deadlock.
 */
static void
lwkt_process_ipiq_nested(void)
{
    globaldata_t gd = mycpu;
    globaldata_t sgd;
    lwkt_ipiq_t ip;
    cpumask_t mask;
    int n;
    int limit;

    ++gd->gd_processing_ipiq;
again:
    mask = gd->gd_ipimask;
    cpu_ccfence();
    while (CPUMASK_TESTNZERO(mask)) {
	n = BSFCPUMASK(mask);
	if (n != gd->gd_cpuid) {
	    sgd = globaldata_find(n);
	    ip = sgd->gd_ipiq;

	    /*
	     * NOTE: We do not mess with the cpumask at all, instead we allow
	     *	     the top-level ipiq processor deal with it.
	     */
	    if (ip != NULL) {
		ip += gd->gd_cpuid;
		if ((limit = ip->ip_drain) != 0) {
		    lwkt_process_ipiq_core(sgd, ip, NULL, limit);
		    /* no gd_ipimask when doing limited processing */
		}
	    }
	}
	CPUMASK_NANDBIT(mask, n);
    }

    /*
     * Process pending cpusyncs.  If the current thread has a cpusync
     * active cpusync we only run the list once and do not re-flag
     * as the thread itself is processing its interlock.
     */
    if (lwkt_process_ipiq_core(gd, &gd->gd_cpusyncq, NULL, 0)) {
	if (gd->gd_curthread->td_cscount == 0)
	    goto again;
	/* need_ipiq(); do not reflag */
    }
    --gd->gd_processing_ipiq;
}

/*
 * Process incoming IPI requests until only <limit> are left (0 to exhaust
 * all incoming IPI requests).
 */
static int
lwkt_process_ipiq_core(globaldata_t sgd, lwkt_ipiq_t ip, 
		       struct intrframe *frame, int limit)
{
    globaldata_t mygd = mycpu;
    int ri;
    int wi;
    ipifunc3_t copy_func;
    void *copy_arg1;
    int copy_arg2;

    /*
     * Clear the originating core from our ipimask, we will process all
     * incoming messages.
     *
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
     *
     * NOTE: Single pass only.  Returns non-zero if the queue is not empty
     *	     on return.
     */
    while (wi - (ri = ip->ip_rindex) > limit) {
	ri &= MAXCPUFIFO_MASK;
	cpu_lfence();
	copy_func = ip->ip_info[ri].func;
	copy_arg1 = ip->ip_info[ri].arg1;
	copy_arg2 = ip->ip_info[ri].arg2;
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
#if defined(__x86_64__)
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
     * Return non-zero if there is still more in the queue.  Don't worry
     * about fencing, we will get another interrupt if necessary.
     */
    return (ip->ip_rindex != ip->ip_windex);
}

static void
lwkt_sync_ipiq(void *arg)
{
    volatile cpumask_t *cpumask = arg;

    ATOMIC_CPUMASK_NANDBIT(*cpumask, mycpu->gd_cpuid);
    if (CPUMASK_TESTZERO(*cpumask))
	wakeup(cpumask);
}

void
lwkt_synchronize_ipiqs(const char *wmesg)
{
    volatile cpumask_t other_cpumask;

    other_cpumask = smp_active_mask;
    CPUMASK_ANDMASK(other_cpumask, mycpu->gd_other_cpus);
    lwkt_send_ipiq_mask(other_cpumask, lwkt_sync_ipiq,
			__DEVOLATILE(void *, &other_cpumask));

    while (CPUMASK_TESTNZERO(other_cpumask)) {
	tsleep_interlock(&other_cpumask, 0);
	if (CPUMASK_TESTNZERO(other_cpumask))
	    tsleep(&other_cpumask, PINTERLOCKED, wmesg, 0);
    }
}

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
    globaldata_t gd = mycpu;
    cpumask_t mask;

    /*
     * mask acknowledge (cs_mack):  0->mask for stage 1
     *
     * mack does not include the current cpu.
     */
    mask = cs->cs_mask;
    CPUMASK_ANDMASK(mask, gd->gd_other_cpus);
    CPUMASK_ANDMASK(mask, smp_active_mask);
    CPUMASK_ASSZERO(cs->cs_mack);

    crit_enter_id("cpusync");
    if (CPUMASK_TESTNZERO(mask)) {
	DEBUG_PUSH_INFO("cpusync_interlock");
	++ipiq_stat(gd).ipiq_cscount;
	++gd->gd_curthread->td_cscount;
	lwkt_send_ipiq_mask(mask, (ipifunc1_t)lwkt_cpusync_remote1, cs);
	logipiq2(sync_start, (long)CPUMASK_LOWMASK(mask));
	while (CPUMASK_CMPMASKNEQ(cs->cs_mack, mask)) {
	    lwkt_process_ipiq();
	    cpu_pause();
#ifdef _KERNEL_VIRTUAL
	    pthread_yield();
#endif
	}
	DEBUG_POP_INFO();
    }
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
    CPUMASK_ASSZERO(cs->cs_mack);
    cpu_ccfence();
    if (cs->cs_func && CPUMASK_TESTBIT(cs->cs_mask, gd->gd_cpuid))
	    cs->cs_func(cs->cs_data);
    if (CPUMASK_TESTNZERO(mask)) {
	DEBUG_PUSH_INFO("cpusync_deinterlock");
	while (CPUMASK_CMPMASKNEQ(cs->cs_mack, mask)) {
	    lwkt_process_ipiq();
	    cpu_pause();
#ifdef _KERNEL_VIRTUAL
	    pthread_yield();
#endif
	}
	DEBUG_POP_INFO();
	/*
	 * cpusyncq ipis may be left queued without the RQF flag set due to
	 * a non-zero td_cscount, so be sure to process any laggards after
	 * decrementing td_cscount.
	 */
	--gd->gd_curthread->td_cscount;
	lwkt_process_ipiq();
	logipiq2(sync_end, (long)CPUMASK_LOWMASK(mask));
    }
    crit_exit_id("cpusync");
}

/*
 * The quick version does not quiesce the target cpu(s) but instead executes
 * the function on the target cpu(s) and waits for all to acknowledge.  This
 * avoids spinning on the target cpus.
 *
 * This function is typically only used for kernel_pmap updates.  User pmaps
 * have to be quiesced.
 */
void
lwkt_cpusync_quick(lwkt_cpusync_t cs)
{
    globaldata_t gd = mycpu;
    cpumask_t mask;

    /*
     * stage-2 cs_mack only.
     */
    mask = cs->cs_mask;
    CPUMASK_ANDMASK(mask, gd->gd_other_cpus);
    CPUMASK_ANDMASK(mask, smp_active_mask);
    CPUMASK_ASSZERO(cs->cs_mack);

    crit_enter_id("cpusync");
    if (CPUMASK_TESTNZERO(mask)) {
	DEBUG_PUSH_INFO("cpusync_interlock");
	++ipiq_stat(gd).ipiq_cscount;
	++gd->gd_curthread->td_cscount;
	lwkt_send_ipiq_mask(mask, (ipifunc1_t)lwkt_cpusync_remote2, cs);
	logipiq2(sync_quick, (long)CPUMASK_LOWMASK(mask));
	while (CPUMASK_CMPMASKNEQ(cs->cs_mack, mask)) {
	    lwkt_process_ipiq();
	    cpu_pause();
#ifdef _KERNEL_VIRTUAL
	    pthread_yield();
#endif
	}

	/*
	 * cpusyncq ipis may be left queued without the RQF flag set due to
	 * a non-zero td_cscount, so be sure to process any laggards after
	 * decrementing td_cscount.
	 */
	DEBUG_POP_INFO();
	--gd->gd_curthread->td_cscount;
	lwkt_process_ipiq();
    }
    if (cs->cs_func && CPUMASK_TESTBIT(cs->cs_mask, gd->gd_cpuid))
	    cs->cs_func(cs->cs_data);
    crit_exit_id("cpusync");
}

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

    ATOMIC_CPUMASK_ORBIT(cs->cs_mack, gd->gd_cpuid);
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

    if (CPUMASK_TESTMASK(cs->cs_mack, gd->gd_cpumask) == 0) {
	if (cs->cs_func)
		cs->cs_func(cs->cs_data);
	ATOMIC_CPUMASK_ORBIT(cs->cs_mack, gd->gd_cpuid);
	/* cs can be ripped out at this point */
    } else {
	lwkt_ipiq_t ip;
	int wi;

	cpu_pause();
#ifdef _KERNEL_VIRTUAL
	pthread_yield();
#endif
	cpu_lfence();

	/*
	 * Requeue our IPI to avoid a deep stack recursion.  If no other
	 * IPIs are pending we can just loop up, which should help VMs
	 * better-detect spin loops.
	 */
	ip = &gd->gd_cpusyncq;

	wi = ip->ip_windex & MAXCPUFIFO_MASK;
	ip->ip_info[wi].func = (ipifunc3_t)(ipifunc1_t)lwkt_cpusync_remote2;
	ip->ip_info[wi].arg1 = cs;
	ip->ip_info[wi].arg2 = 0;
	cpu_sfence();
	KKASSERT(ip->ip_windex - ip->ip_rindex < MAXCPUFIFO);
	++ip->ip_windex;
	if (ipiq_debug && (ip->ip_windex & 0xFFFFFF) == 0) {
		kprintf("cpu %d cm=%016jx %016jx f=%p\n",
			gd->gd_cpuid,
			(intmax_t)CPUMASK_LOWMASK(cs->cs_mask),
			(intmax_t)CPUMASK_LOWMASK(cs->cs_mack),
			cs->cs_func);
	}
    }
}

#define LWKT_IPIQ_NLATENCY	8
#define LWKT_IPIQ_NLATENCY_MASK	(LWKT_IPIQ_NLATENCY - 1)

struct lwkt_ipiq_latency_log {
	int		idx;	/* unmasked index */
	int		pad;
	uint64_t	latency[LWKT_IPIQ_NLATENCY];
};

static struct lwkt_ipiq_latency_log	lwkt_ipiq_latency_logs[MAXCPU];
static uint64_t save_tsc;

/*
 * IPI callback (already in a critical section)
 */
static void
lwkt_ipiq_latency_testfunc(void *arg __unused)
{
	uint64_t delta_tsc;
	struct globaldata *gd;
	struct lwkt_ipiq_latency_log *lat;

	/*
	 * Get delta TSC (assume TSCs are synchronized) as quickly as
	 * possible and then convert to nanoseconds.
	 */
	delta_tsc = rdtsc_ordered() - save_tsc;
	delta_tsc = delta_tsc * 1000000000LU / tsc_frequency;

	/*
	 * Record in our save array.
	 */
	gd = mycpu;
	lat = &lwkt_ipiq_latency_logs[gd->gd_cpuid];
	lat->latency[lat->idx & LWKT_IPIQ_NLATENCY_MASK] = delta_tsc;
	++lat->idx;
}

/*
 * Send IPI from cpu0 to other cpus
 *
 * NOTE: Machine must be idle for test to run dependably, and also probably
 *	 a good idea not to be running powerd.
 *
 * NOTE: Caller should use 'usched :1 <command>' to lock itself to cpu 0.
 *	 See 'ipitest' script in /usr/src/test/sysperf/ipitest
 */
static int
lwkt_ipiq_latency_test(SYSCTL_HANDLER_ARGS)
{
	struct globaldata *gd;
	int cpu = 0, orig_cpu, error;

	error = sysctl_handle_int(oidp, &cpu, arg2, req);
	if (error || req->newptr == NULL)
		return error;

	if (cpu == 0)
		return 0;
	else if (cpu >= ncpus || cpu < 0)
		return EINVAL;

	orig_cpu = mycpuid;
	lwkt_migratecpu(0);

	gd = globaldata_find(cpu);

	save_tsc = rdtsc_ordered();
	lwkt_send_ipiq(gd, lwkt_ipiq_latency_testfunc, NULL);

	lwkt_migratecpu(orig_cpu);
	return 0;
}

SYSCTL_NODE(_debug, OID_AUTO, ipiq, CTLFLAG_RW, 0, "");
SYSCTL_PROC(_debug_ipiq, OID_AUTO, latency_test, CTLTYPE_INT | CTLFLAG_RW,
    NULL, 0, lwkt_ipiq_latency_test, "I",
    "ipi latency test, arg: remote cpuid");

static int
lwkt_ipiq_latency(SYSCTL_HANDLER_ARGS)
{
	struct lwkt_ipiq_latency_log *latency = arg1;
	uint64_t lat[LWKT_IPIQ_NLATENCY];
	int i;

	for (i = 0; i < LWKT_IPIQ_NLATENCY; ++i)
		lat[i] = latency->latency[i];

	return sysctl_handle_opaque(oidp, lat, sizeof(lat), req);
}

static void
lwkt_ipiq_latency_init(void *dummy __unused)
{
	int cpu;

	for (cpu = 0; cpu < ncpus; ++cpu) {
		char name[32];

		ksnprintf(name, sizeof(name), "latency%d", cpu);
		SYSCTL_ADD_PROC(NULL, SYSCTL_STATIC_CHILDREN(_debug_ipiq),
		    OID_AUTO, name, CTLTYPE_OPAQUE | CTLFLAG_RD,
		    &lwkt_ipiq_latency_logs[cpu], 0, lwkt_ipiq_latency,
		    "LU", "7 latest ipi latency measurement results");
	}
}
SYSINIT(lwkt_ipiq_latency, SI_SUB_CONFIGURE, SI_ORDER_ANY,
    lwkt_ipiq_latency_init, NULL);
