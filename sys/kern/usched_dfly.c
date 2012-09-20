/*
 * Copyright (c) 2012 The DragonFly Project.  All rights reserved.
 * Copyright (c) 1999 Peter Wemm <peter@FreeBSD.org>.  All rights reserved.
 *
 * This code is derived from software contributed to The DragonFly Project
 * by Matthew Dillon <dillon@backplane.com>,
 * by Mihai Carabas <mihai.carabas@gmail.com>
 * and many others.
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
#include <sys/param.h>
#include <sys/systm.h>
#include <sys/kernel.h>
#include <sys/lock.h>
#include <sys/queue.h>
#include <sys/proc.h>
#include <sys/rtprio.h>
#include <sys/uio.h>
#include <sys/sysctl.h>
#include <sys/resourcevar.h>
#include <sys/spinlock.h>
#include <sys/cpu_topology.h>
#include <sys/thread2.h>
#include <sys/spinlock2.h>
#include <sys/mplock2.h>

#include <sys/ktr.h>

#include <machine/cpu.h>
#include <machine/smp.h>

/*
 * Priorities.  Note that with 32 run queues per scheduler each queue
 * represents four priority levels.
 */

int dfly_rebalanced;

#define MAXPRI			128
#define PRIMASK			(MAXPRI - 1)
#define PRIBASE_REALTIME	0
#define PRIBASE_NORMAL		MAXPRI
#define PRIBASE_IDLE		(MAXPRI * 2)
#define PRIBASE_THREAD		(MAXPRI * 3)
#define PRIBASE_NULL		(MAXPRI * 4)

#define NQS	32			/* 32 run queues. */
#define PPQ	(MAXPRI / NQS)		/* priorities per queue */
#define PPQMASK	(PPQ - 1)

/*
 * NICEPPQ	- number of nice units per priority queue
 * ESTCPUPPQ	- number of estcpu units per priority queue
 * ESTCPUMAX	- number of estcpu units
 */
#define NICEPPQ		2
#define ESTCPUPPQ	512
#define ESTCPUMAX	(ESTCPUPPQ * NQS)
#define BATCHMAX	(ESTCPUFREQ * 30)
#define PRIO_RANGE	(PRIO_MAX - PRIO_MIN + 1)

#define ESTCPULIM(v)	min((v), ESTCPUMAX)

TAILQ_HEAD(rq, lwp);

#define lwp_priority	lwp_usdata.dfly.priority
#define lwp_forked	lwp_usdata.dfly.forked
#define lwp_rqindex	lwp_usdata.dfly.rqindex
#define lwp_estcpu	lwp_usdata.dfly.estcpu
#define lwp_batch	lwp_usdata.dfly.batch
#define lwp_rqtype	lwp_usdata.dfly.rqtype
#define lwp_qcpu	lwp_usdata.dfly.qcpu

struct usched_dfly_pcpu {
	struct spinlock spin;
	struct thread	helper_thread;
	short		rrcount;
	short		upri;
	int		uload;
	struct lwp	*uschedcp;
	struct rq	queues[NQS];
	struct rq	rtqueues[NQS];
	struct rq	idqueues[NQS];
	u_int32_t	queuebits;
	u_int32_t	rtqueuebits;
	u_int32_t	idqueuebits;
	int		runqcount;
	int		cpuid;
	cpumask_t	cpumask;
#ifdef SMP
	cpu_node_t	*cpunode;
#endif
};

typedef struct usched_dfly_pcpu	*dfly_pcpu_t;

static void dfly_acquire_curproc(struct lwp *lp);
static void dfly_release_curproc(struct lwp *lp);
static void dfly_select_curproc(globaldata_t gd);
static void dfly_setrunqueue(struct lwp *lp);
static void dfly_setrunqueue_dd(dfly_pcpu_t rdd, struct lwp *lp);
static void dfly_schedulerclock(struct lwp *lp, sysclock_t period,
				sysclock_t cpstamp);
static void dfly_recalculate_estcpu(struct lwp *lp);
static void dfly_resetpriority(struct lwp *lp);
static void dfly_forking(struct lwp *plp, struct lwp *lp);
static void dfly_exiting(struct lwp *lp, struct proc *);
static void dfly_uload_update(struct lwp *lp);
static void dfly_yield(struct lwp *lp);
#ifdef SMP
static dfly_pcpu_t dfly_choose_best_queue(struct lwp *lp);
static dfly_pcpu_t dfly_choose_worst_queue(dfly_pcpu_t dd, int weight);
static dfly_pcpu_t dfly_choose_queue_simple(dfly_pcpu_t dd, struct lwp *lp);
#endif

#ifdef SMP
static void dfly_need_user_resched_remote(void *dummy);
#endif
static struct lwp *dfly_chooseproc_locked(dfly_pcpu_t rdd, dfly_pcpu_t dd,
					  struct lwp *chklp, int worst);
static void dfly_remrunqueue_locked(dfly_pcpu_t dd, struct lwp *lp);
static void dfly_setrunqueue_locked(dfly_pcpu_t dd, struct lwp *lp);

struct usched usched_dfly = {
	{ NULL },
	"dfly", "Original DragonFly Scheduler",
	NULL,			/* default registration */
	NULL,			/* default deregistration */
	dfly_acquire_curproc,
	dfly_release_curproc,
	dfly_setrunqueue,
	dfly_schedulerclock,
	dfly_recalculate_estcpu,
	dfly_resetpriority,
	dfly_forking,
	dfly_exiting,
	dfly_uload_update,
	NULL,			/* setcpumask not supported */
	dfly_yield
};

/*
 * We have NQS (32) run queues per scheduling class.  For the normal
 * class, there are 128 priorities scaled onto these 32 queues.  New
 * processes are added to the last entry in each queue, and processes
 * are selected for running by taking them from the head and maintaining
 * a simple FIFO arrangement.  Realtime and Idle priority processes have
 * and explicit 0-31 priority which maps directly onto their class queue
 * index.  When a queue has something in it, the corresponding bit is
 * set in the queuebits variable, allowing a single read to determine
 * the state of all 32 queues and then a ffs() to find the first busy
 * queue.
 */
static cpumask_t dfly_curprocmask = -1;	/* currently running a user process */
static cpumask_t dfly_rdyprocmask;	/* ready to accept a user process */
#ifdef SMP
static volatile int dfly_scancpu;
#endif
static struct usched_dfly_pcpu dfly_pcpu[MAXCPU];
static struct sysctl_ctx_list usched_dfly_sysctl_ctx;
static struct sysctl_oid *usched_dfly_sysctl_tree;

/* Debug info exposed through debug.* sysctl */

static int usched_dfly_debug = -1;
SYSCTL_INT(_debug, OID_AUTO, dfly_scdebug, CTLFLAG_RW,
	   &usched_dfly_debug, 0,
	   "Print debug information for this pid");

static int usched_dfly_pid_debug = -1;
SYSCTL_INT(_debug, OID_AUTO, dfly_pid_debug, CTLFLAG_RW,
	   &usched_dfly_pid_debug, 0,
	   "Print KTR debug information for this pid");

static int usched_dfly_chooser = 0;
SYSCTL_INT(_debug, OID_AUTO, dfly_chooser, CTLFLAG_RW,
	   &usched_dfly_chooser, 0,
	   "Print KTR debug information for this pid");

/*
 * Tunning usched_dfly - configurable through kern.usched_dfly.
 *
 * weight1 - Tries to keep threads on their current cpu.  If you
 *	     make this value too large the scheduler will not be
 *	     able to load-balance large loads.
 *
 * weight2 - If non-zero, detects thread pairs undergoing synchronous
 *	     communications and tries to move them closer together.
 *	     This only matters under very heavy loads because if there
 *	     are plenty of cpu's available the pairs will be placed
 *	     on separate cpu's.  ONLY APPLIES TO PROCESS PAIRS UNDERGOING
 *	     SYNCHRONOUS COMMUNICATIONS!  e.g. client/server on same host.
 *
 *	     Given A x N processes and B x N processes (for 2*N total),
 *	     any low value of N up to around the number of hw threads
 *	     in the system, '15' is a good setting, because you don't want
 *	     to force process pairs together when you have tons of cpu
 *	     cores available.
 *
 *	     For heavier loads, '35' is a good setting which will still
 *	     be fairly optimal at lighter loads.
 *
 *	     For extreme loads, '55' is a good setting because you want to
 *	     force the pairs together.
 *
 *	     15: Fewer threads.
 *	     35: Heavily loaded.	(default)
 *	     50: Very heavily loaded.
 *
 * weight3 - Weighting based on the number of runnable threads on the
 *	     userland scheduling queue and ignoring their loads.
 *	     A nominal value here prevents high-priority (low-load)
 *	     threads from accumulating on one cpu core when other
 *	     cores are available.
 */
#ifdef SMP
static int usched_dfly_smt = 0;
static int usched_dfly_cache_coherent = 0;
static int usched_dfly_weight1 = 50;	/* keep thread on current cpu */
static int usched_dfly_weight2 = 35;	/* synchronous peer's current cpu */
static int usched_dfly_weight3 = 10;	/* number of threads on queue */
static int usched_dfly_features = 15;	/* allow pulls */
#endif
static int usched_dfly_rrinterval = (ESTCPUFREQ + 9) / 10;
static int usched_dfly_decay = 8;
static int usched_dfly_batch_time = 10;

/* KTR debug printings */

KTR_INFO_MASTER(usched);

#if !defined(KTR_USCHED_DFLY)
#define	KTR_USCHED_DFLY	KTR_ALL
#endif

#if 0
KTR_INFO(KTR_USCHED_DFLY, usched, dfly_acquire_curproc_urw, 0,
    "USCHED_DFLY(dfly_acquire_curproc in user_reseched_wanted "
    "after release: pid %d, cpuid %d, curr_cpuid %d)",
    pid_t pid, int cpuid, int curr);
KTR_INFO(KTR_USCHED_DFLY, usched, dfly_acquire_curproc_before_loop, 0,
    "USCHED_DFLY(dfly_acquire_curproc before loop: pid %d, cpuid %d, "
    "curr_cpuid %d)",
    pid_t pid, int cpuid, int curr);
KTR_INFO(KTR_USCHED_DFLY, usched, dfly_acquire_curproc_not, 0,
    "USCHED_DFLY(dfly_acquire_curproc couldn't acquire after "
    "dfly_setrunqueue: pid %d, cpuid %d, curr_lp pid %d, curr_cpuid %d)",
    pid_t pid, int cpuid, pid_t curr_pid, int curr_cpuid);
KTR_INFO(KTR_USCHED_DFLY, usched, dfly_acquire_curproc_switch, 0,
    "USCHED_DFLY(dfly_acquire_curproc after lwkt_switch: pid %d, "
    "cpuid %d, curr_cpuid %d)",
    pid_t pid, int cpuid, int curr);

KTR_INFO(KTR_USCHED_DFLY, usched, dfly_release_curproc, 0,
    "USCHED_DFLY(dfly_release_curproc before select: pid %d, "
    "cpuid %d, curr_cpuid %d)",
    pid_t pid, int cpuid, int curr);

KTR_INFO(KTR_USCHED_DFLY, usched, dfly_select_curproc, 0,
    "USCHED_DFLY(dfly_release_curproc before select: pid %d, "
    "cpuid %d, old_pid %d, old_cpuid %d, curr_cpuid %d)",
    pid_t pid, int cpuid, pid_t old_pid, int old_cpuid, int curr);

#ifdef SMP
KTR_INFO(KTR_USCHED_DFLY, usched, batchy_test_false, 0,
    "USCHED_DFLY(batchy_looser_pri_test false: pid %d, "
    "cpuid %d, verify_mask %lu)",
    pid_t pid, int cpuid, cpumask_t mask);
KTR_INFO(KTR_USCHED_DFLY, usched, batchy_test_true, 0,
    "USCHED_DFLY(batchy_looser_pri_test true: pid %d, "
    "cpuid %d, verify_mask %lu)",
    pid_t pid, int cpuid, cpumask_t mask);

KTR_INFO(KTR_USCHED_DFLY, usched, dfly_setrunqueue_fc_smt, 0,
    "USCHED_DFLY(dfly_setrunqueue free cpus smt: pid %d, cpuid %d, "
    "mask %lu, curr_cpuid %d)",
    pid_t pid, int cpuid, cpumask_t mask, int curr);
KTR_INFO(KTR_USCHED_DFLY, usched, dfly_setrunqueue_fc_non_smt, 0,
    "USCHED_DFLY(dfly_setrunqueue free cpus check non_smt: pid %d, "
    "cpuid %d, mask %lu, curr_cpuid %d)",
    pid_t pid, int cpuid, cpumask_t mask, int curr);
KTR_INFO(KTR_USCHED_DFLY, usched, dfly_setrunqueue_rc, 0,
    "USCHED_DFLY(dfly_setrunqueue running cpus check: pid %d, "
    "cpuid %d, mask %lu, curr_cpuid %d)",
    pid_t pid, int cpuid, cpumask_t mask, int curr);
KTR_INFO(KTR_USCHED_DFLY, usched, dfly_setrunqueue_found, 0,
    "USCHED_DFLY(dfly_setrunqueue found cpu: pid %d, cpuid %d, "
    "mask %lu, found_cpuid %d, curr_cpuid %d)",
    pid_t pid, int cpuid, cpumask_t mask, int found_cpuid, int curr);
KTR_INFO(KTR_USCHED_DFLY, usched, dfly_setrunqueue_not_found, 0,
    "USCHED_DFLY(dfly_setrunqueue not found cpu: pid %d, cpuid %d, "
    "try_cpuid %d, curr_cpuid %d)",
    pid_t pid, int cpuid, int try_cpuid, int curr);
KTR_INFO(KTR_USCHED_DFLY, usched, dfly_setrunqueue_found_best_cpuid, 0,
    "USCHED_DFLY(dfly_setrunqueue found cpu: pid %d, cpuid %d, "
    "mask %lu, found_cpuid %d, curr_cpuid %d)",
    pid_t pid, int cpuid, cpumask_t mask, int found_cpuid, int curr);
#endif
#endif

KTR_INFO(KTR_USCHED_DFLY, usched, chooseproc, 0,
    "USCHED_DFLY(chooseproc: pid %d, old_cpuid %d, curr_cpuid %d)",
    pid_t pid, int old_cpuid, int curr);
#ifdef SMP
#if 0
KTR_INFO(KTR_USCHED_DFLY, usched, chooseproc_cc, 0,
    "USCHED_DFLY(chooseproc_cc: pid %d, old_cpuid %d, curr_cpuid %d)",
    pid_t pid, int old_cpuid, int curr);
KTR_INFO(KTR_USCHED_DFLY, usched, chooseproc_cc_not_good, 0,
    "USCHED_DFLY(chooseproc_cc not good: pid %d, old_cpumask %lu, "
    "sibling_mask %lu, curr_cpumask %lu)",
    pid_t pid, cpumask_t old_cpumask, cpumask_t sibling_mask, cpumask_t curr);
KTR_INFO(KTR_USCHED_DFLY, usched, chooseproc_cc_elected, 0,
    "USCHED_DFLY(chooseproc_cc elected: pid %d, old_cpumask %lu, "
    "sibling_mask %lu, curr_cpumask: %lu)",
    pid_t pid, cpumask_t old_cpumask, cpumask_t sibling_mask, cpumask_t curr);

KTR_INFO(KTR_USCHED_DFLY, usched, sched_thread_no_process, 0,
    "USCHED_DFLY(sched_thread %d no process scheduled: pid %d, old_cpuid %d)",
    int id, pid_t pid, int cpuid);
KTR_INFO(KTR_USCHED_DFLY, usched, sched_thread_process, 0,
    "USCHED_DFLY(sched_thread %d process scheduled: pid %d, old_cpuid %d)",
    int id, pid_t pid, int cpuid);
KTR_INFO(KTR_USCHED_DFLY, usched, sched_thread_no_process_found, 0,
    "USCHED_DFLY(sched_thread %d no process found; tmpmask %lu)",
    int id, cpumask_t tmpmask);
#endif
#endif

/*
 * DFLY_ACQUIRE_CURPROC
 *
 * This function is called when the kernel intends to return to userland.
 * It is responsible for making the thread the current designated userland
 * thread for this cpu, blocking if necessary.
 *
 * The kernel has already depressed our LWKT priority so we must not switch
 * until we have either assigned or disposed of the thread.
 *
 * WARNING! THIS FUNCTION IS ALLOWED TO CAUSE THE CURRENT THREAD TO MIGRATE
 * TO ANOTHER CPU!  Because most of the kernel assumes that no migration will
 * occur, this function is called only under very controlled circumstances.
 */
static void
dfly_acquire_curproc(struct lwp *lp)
{
	globaldata_t gd;
	dfly_pcpu_t dd;
	dfly_pcpu_t rdd;
	thread_t td;
	int doresched;

	/*
	 * Make sure we aren't sitting on a tsleep queue.
	 */
	td = lp->lwp_thread;
	crit_enter_quick(td);
	if (td->td_flags & TDF_TSLEEPQ)
		tsleep_remove(td);
	dfly_recalculate_estcpu(lp);

	/*
	 * If a reschedule was requested give another thread the
	 * driver's seat.
	 */
	if (user_resched_wanted()) {
		clear_user_resched();
		dfly_release_curproc(lp);
		doresched = 1;
	} else {
		doresched = 0;
	}

	/*
	 * Loop until we are the current user thread
	 */
	gd = mycpu;
	dd = &dfly_pcpu[gd->gd_cpuid];

	do {
		/*
		 * Process any pending events and higher priority threads
		 * only.  Do not try to round-robin same-priority lwkt
		 * threads.
		 */
		lwkt_yield_quick();

		/*
		 * Become the currently scheduled user thread for this cpu
		 * if we can do so trivially.
		 *
		 * We can steal another thread's current thread designation
		 * on this cpu since if we are running that other thread
		 * must not be, so we can safely deschedule it.
		 */
		if (dd->uschedcp == lp) {
			/*
			 * We are already the current lwp (hot path).
			 */
			dd->upri = lp->lwp_priority;
			continue;
		}
		if (doresched && (rdd = dfly_choose_best_queue(lp)) != dd) {
			/*
			 * We are not or are no longer the current lwp and
			 * a reschedule was requested.  Figure out the
			 * best cpu to run on (our current cpu will be
			 * given significant weight).
			 *
			 * (if a reschedule was not requested we want to
			 * move this step after the uschedcp tests).
			 */
			lwkt_deschedule(lp->lwp_thread);
			dfly_setrunqueue_dd(rdd, lp);
			lwkt_switch();
			gd = mycpu;
			dd = &dfly_pcpu[gd->gd_cpuid];
			continue;
		}
		spin_lock(&dd->spin);
		if (dd->uschedcp == NULL) {
			/*
			 * Either no reschedule was requested or the best
			 * queue was dd, and no current process has been
			 * selected.  We can trivially become the current
			 * lwp on the current cpu.
			 */
			atomic_set_cpumask(&dfly_curprocmask,
					   gd->gd_cpumask);
			dd->uschedcp = lp;
			dd->upri = lp->lwp_priority;
			KKASSERT(lp->lwp_qcpu == dd->cpuid);
			spin_unlock(&dd->spin);
			continue;
		}
		if (dd->uschedcp &&
		   (dd->upri & ~PPQMASK) >
		   (lp->lwp_priority & ~PPQMASK)) {
			/*
			 * Can we steal the current designated user thread?
			 *
			 * If we do the other thread will stall when it tries
			 * to return to userland, possibly rescheduling
			 * elsewhere.
			 *
			 * It is important to do a masked test to
			 * avoid the edge case where two
			 * near-equal-priority threads
			 * are constantly interrupting each other.
			 */
			dd->uschedcp = lp;
			dd->upri = lp->lwp_priority;
			KKASSERT(lp->lwp_qcpu == dd->cpuid);
			spin_unlock(&dd->spin);
			continue;
		}
		if (doresched == 0 &&
		    (rdd = dfly_choose_best_queue(lp)) != dd) {
			/*
			 * We are not the current lwp, figure out the
			 * best cpu to run on (our current cpu will be
			 * given significant weight).  Loop on cpu change.
			 */
			spin_unlock(&dd->spin);
			lwkt_deschedule(lp->lwp_thread);
			dfly_setrunqueue_dd(rdd, lp);
			lwkt_switch();
			gd = mycpu;
			dd = &dfly_pcpu[gd->gd_cpuid];
		} else {
			/*
			 * We cannot become the current lwp, place
			 * the lp on the run-queue of this or another
			 * cpu and deschedule ourselves.
			 *
			 * When we are reactivated we will have another
			 * chance.
			 *
			 * Reload after a switch or setrunqueue/switch
			 * possibly moved us to another cpu.
			 */
			spin_unlock(&dd->spin);
			lwkt_deschedule(lp->lwp_thread);
			dfly_setrunqueue_dd(dd, lp);
			lwkt_switch();
			gd = mycpu;
			dd = &dfly_pcpu[gd->gd_cpuid];
		}
	} while (dd->uschedcp != lp);

	crit_exit_quick(td);
	KKASSERT((lp->lwp_mpflags & LWP_MP_ONRUNQ) == 0);
}

/*
 * DFLY_RELEASE_CURPROC
 *
 * This routine detaches the current thread from the userland scheduler,
 * usually because the thread needs to run or block in the kernel (at
 * kernel priority) for a while.
 *
 * This routine is also responsible for selecting a new thread to
 * make the current thread.
 *
 * NOTE: This implementation differs from the dummy example in that
 * dfly_select_curproc() is able to select the current process, whereas
 * dummy_select_curproc() is not able to select the current process.
 * This means we have to NULL out uschedcp.
 *
 * Additionally, note that we may already be on a run queue if releasing
 * via the lwkt_switch() in dfly_setrunqueue().
 */
static void
dfly_release_curproc(struct lwp *lp)
{
	globaldata_t gd = mycpu;
	dfly_pcpu_t dd = &dfly_pcpu[gd->gd_cpuid];

	/*
	 * Make sure td_wakefromcpu is defaulted.  This will be overwritten
	 * by wakeup().
	 */
	lp->lwp_thread->td_wakefromcpu = gd->gd_cpuid;

	if (dd->uschedcp == lp) {
		KKASSERT((lp->lwp_mpflags & LWP_MP_ONRUNQ) == 0);
		spin_lock(&dd->spin);
		if (dd->uschedcp == lp) {
			dd->uschedcp = NULL;	/* don't let lp be selected */
			dd->upri = PRIBASE_NULL;
			atomic_clear_cpumask(&dfly_curprocmask, gd->gd_cpumask);
			spin_unlock(&dd->spin);
			dfly_select_curproc(gd);
		} else {
			spin_unlock(&dd->spin);
		}
	}
}

/*
 * DFLY_SELECT_CURPROC
 *
 * Select a new current process for this cpu and clear any pending user
 * reschedule request.  The cpu currently has no current process.
 *
 * This routine is also responsible for equal-priority round-robining,
 * typically triggered from dfly_schedulerclock().  In our dummy example
 * all the 'user' threads are LWKT scheduled all at once and we just
 * call lwkt_switch().
 *
 * The calling process is not on the queue and cannot be selected.
 */
static
void
dfly_select_curproc(globaldata_t gd)
{
	dfly_pcpu_t dd = &dfly_pcpu[gd->gd_cpuid];
	struct lwp *nlp;
	int cpuid = gd->gd_cpuid;

	crit_enter_gd(gd);

	spin_lock(&dd->spin);
	nlp = dfly_chooseproc_locked(dd, dd, dd->uschedcp, 0);

	if (nlp) {
		atomic_set_cpumask(&dfly_curprocmask, CPUMASK(cpuid));
		dd->upri = nlp->lwp_priority;
		dd->uschedcp = nlp;
		dd->rrcount = 0;		/* reset round robin */
		spin_unlock(&dd->spin);
#ifdef SMP
		lwkt_acquire(nlp->lwp_thread);
#endif
		lwkt_schedule(nlp->lwp_thread);
	} else {
		spin_unlock(&dd->spin);
	}
	crit_exit_gd(gd);
}

/*
 * Place the specified lwp on the user scheduler's run queue.  This routine
 * must be called with the thread descheduled.  The lwp must be runnable.
 * It must not be possible for anyone else to explicitly schedule this thread.
 *
 * The thread may be the current thread as a special case.
 */
static void
dfly_setrunqueue(struct lwp *lp)
{
	dfly_pcpu_t rdd;

	/*
	 * First validate the process LWKT state.
	 */
	KASSERT(lp->lwp_stat == LSRUN, ("setrunqueue: lwp not LSRUN"));
	KASSERT((lp->lwp_mpflags & LWP_MP_ONRUNQ) == 0,
	    ("lwp %d/%d already on runq! flag %08x/%08x", lp->lwp_proc->p_pid,
	     lp->lwp_tid, lp->lwp_proc->p_flags, lp->lwp_flags));
	KKASSERT((lp->lwp_thread->td_flags & TDF_RUNQ) == 0);

	/*
	 * NOTE: rdd does not necessarily represent the current cpu.
	 *	 Instead it represents the cpu the thread was last
	 *	 scheduled on.
	 */
	rdd = &dfly_pcpu[lp->lwp_qcpu];

	/*
	 * This process is not supposed to be scheduled anywhere or assigned
	 * as the current process anywhere.  Assert the condition.
	 */
	KKASSERT(rdd->uschedcp != lp);

#ifndef SMP
	/*
	 * If we are not SMP we do not have a scheduler helper to kick
	 * and must directly activate the process if none are scheduled.
	 *
	 * This is really only an issue when bootstrapping init since
	 * the caller in all other cases will be a user process, and
	 * even if released (rdd->uschedcp == NULL), that process will
	 * kickstart the scheduler when it returns to user mode from
	 * the kernel.
	 *
	 * NOTE: On SMP we can't just set some other cpu's uschedcp.
	 */
	if (rdd->uschedcp == NULL) {
		spin_lock(&rdd->spin);
		if (rdd->uschedcp == NULL) {
			atomic_set_cpumask(&dfly_curprocmask, 1);
			rdd->uschedcp = lp;
			rdd->upri = lp->lwp_priority;
			spin_unlock(&rdd->spin);
			lwkt_schedule(lp->lwp_thread);
			return;
		}
		spin_unlock(&rdd->spin);
	}
#endif

#ifdef SMP
	/*
	 * Ok, we have to setrunqueue some target cpu and request a reschedule
	 * if necessary.
	 *
	 * We have to choose the best target cpu.  It might not be the current
	 * target even if the current cpu has no running user thread (for
	 * example, because the current cpu might be a hyperthread and its
	 * sibling has a thread assigned).
	 *
	 * If we just forked it is most optimal to run the child on the same
	 * cpu just in case the parent decides to wait for it (thus getting
	 * off that cpu).  As long as there is nothing else runnable on the
	 * cpu, that is.  If we did this unconditionally a parent forking
	 * multiple children before waiting (e.g. make -j N) leaves other
	 * cpus idle that could be working.
	 */
	if (lp->lwp_forked) {
		lp->lwp_forked = 0;
		if (dfly_pcpu[lp->lwp_qcpu].runqcount)
			rdd = dfly_choose_best_queue(lp);
		else
			rdd = &dfly_pcpu[lp->lwp_qcpu];
		/* dfly_wakeup_random_helper(rdd); */
	} else {
		rdd = dfly_choose_best_queue(lp);
		/* rdd = &dfly_pcpu[lp->lwp_qcpu]; */
	}
#endif
	dfly_setrunqueue_dd(rdd, lp);
}

static void
dfly_setrunqueue_dd(dfly_pcpu_t rdd, struct lwp *lp)
{
#ifdef SMP
	globaldata_t rgd;

	/*
	 * We might be moving the lp to another cpu's run queue, and once
	 * on the runqueue (even if it is our cpu's), another cpu can rip
	 * it away from us.
	 *
	 * TDF_MIGRATING might already be set if this is part of a
	 * remrunqueue+setrunqueue sequence.
	 */
	if ((lp->lwp_thread->td_flags & TDF_MIGRATING) == 0)
		lwkt_giveaway(lp->lwp_thread);

	rgd = globaldata_find(rdd->cpuid);

	/*
	 * We lose control of the lp the moment we release the spinlock
	 * after having placed it on the queue.  i.e. another cpu could pick
	 * it up, or it could exit, or its priority could be further
	 * adjusted, or something like that.
	 *
	 * WARNING! rdd can point to a foreign cpu!
	 */
	spin_lock(&rdd->spin);
	dfly_setrunqueue_locked(rdd, lp);

	if (rgd == mycpu) {
		if ((rdd->upri & ~PPQMASK) > (lp->lwp_priority & ~PPQMASK)) {
			spin_unlock(&rdd->spin);
			if (rdd->uschedcp == NULL) {
				wakeup_mycpu(&rdd->helper_thread); /* XXX */
				need_user_resched();
			} else {
				need_user_resched();
			}
		} else {
			spin_unlock(&rdd->spin);
		}
	} else {
		atomic_clear_cpumask(&dfly_rdyprocmask, rgd->gd_cpumask);
		if ((rdd->upri & ~PPQMASK) > (lp->lwp_priority & ~PPQMASK)) {
			spin_unlock(&rdd->spin);
			lwkt_send_ipiq(rgd, dfly_need_user_resched_remote,
				       NULL);
		} else {
			spin_unlock(&rdd->spin);
			wakeup(&rdd->helper_thread);
		}
	}
#else
	/*
	 * Request a reschedule if appropriate.
	 */
	spin_lock(&rdd->spin);
	dfly_setrunqueue_locked(rdd, lp);
	spin_unlock(&rdd->spin);
	if ((rdd->upri & ~PPQMASK) > (lp->lwp_priority & ~PPQMASK)) {
		need_user_resched();
	}
#endif
}

/*
 * This routine is called from a systimer IPI.  It MUST be MP-safe and
 * the BGL IS NOT HELD ON ENTRY.  This routine is called at ESTCPUFREQ on
 * each cpu.
 */
static
void
dfly_schedulerclock(struct lwp *lp, sysclock_t period, sysclock_t cpstamp)
{
	globaldata_t gd = mycpu;
	dfly_pcpu_t dd = &dfly_pcpu[gd->gd_cpuid];

	/*
	 * Spinlocks also hold a critical section so there should not be
	 * any active.
	 */
	KKASSERT(gd->gd_spinlocks_wr == 0);

	/*
	 * Do we need to round-robin?  We round-robin 10 times a second.
	 * This should only occur for cpu-bound batch processes.
	 */
	if (++dd->rrcount >= usched_dfly_rrinterval) {
		dd->rrcount = 0;
		need_user_resched();
	}

	/*
	 * Adjust estcpu upward using a real time equivalent calculation,
	 * and recalculate lp's priority.
	 */
	lp->lwp_estcpu = ESTCPULIM(lp->lwp_estcpu + ESTCPUMAX / ESTCPUFREQ + 1);
	dfly_resetpriority(lp);

	/*
	 * Rebalance cpus on each scheduler tick.  Each cpu in turn will
	 * calculate the worst queue and, if sufficiently loaded, will
	 * pull a process from that queue into our current queue.
	 *
	 * To try to avoid always moving the same thread. XXX
	 */
#ifdef SMP
	if ((usched_dfly_features & 0x04) &&
	    ((uint16_t)sched_ticks % ncpus) == gd->gd_cpuid) {
		/*
		 * Our cpu is up.
		 */
		struct lwp *nlp;
		dfly_pcpu_t rdd;

		/*
		 * We have to choose the worst thread in the worst queue
		 * because it likely finished its batch on that cpu and is
		 * now waiting for cpu again.
		 */
		rdd = dfly_choose_worst_queue(dd, usched_dfly_weight1 * 4);
		if (rdd && spin_trylock(&rdd->spin)) {
			nlp = dfly_chooseproc_locked(rdd, dd, NULL, 1);
			spin_unlock(&rdd->spin);
		} else {
			nlp = NULL;
		}

		/*
		 * Either schedule it or add it to our queue.
		 */
		if (nlp)
			spin_lock(&dd->spin);
		if (nlp &&
		    (nlp->lwp_priority & ~PPQMASK) < (dd->upri & ~PPQMASK)) {
			atomic_set_cpumask(&dfly_curprocmask, dd->cpumask);
			dd->upri = nlp->lwp_priority;
			dd->uschedcp = nlp;
			dd->rrcount = 0;	/* reset round robin */
			spin_unlock(&dd->spin);
			lwkt_acquire(nlp->lwp_thread);
			lwkt_schedule(nlp->lwp_thread);
		} else if (nlp) {
			dfly_setrunqueue_locked(dd, nlp);
			spin_unlock(&dd->spin);
		}
	}
#endif
}

/*
 * Called from acquire and from kern_synch's one-second timer (one of the
 * callout helper threads) with a critical section held.
 *
 * Decay p_estcpu based on the number of ticks we haven't been running
 * and our p_nice.  As the load increases each process observes a larger
 * number of idle ticks (because other processes are running in them).
 * This observation leads to a larger correction which tends to make the
 * system more 'batchy'.
 *
 * Note that no recalculation occurs for a process which sleeps and wakes
 * up in the same tick.  That is, a system doing thousands of context
 * switches per second will still only do serious estcpu calculations
 * ESTCPUFREQ times per second.
 */
static
void
dfly_recalculate_estcpu(struct lwp *lp)
{
	globaldata_t gd = mycpu;
	dfly_pcpu_t dd = &dfly_pcpu[gd->gd_cpuid];
	sysclock_t cpbase;
	sysclock_t ttlticks;
	int estcpu;
	int decay_factor;

	/*
	 * We have to subtract periodic to get the last schedclock
	 * timeout time, otherwise we would get the upcoming timeout.
	 * Keep in mind that a process can migrate between cpus and
	 * while the scheduler clock should be very close, boundary
	 * conditions could lead to a small negative delta.
	 */
	cpbase = gd->gd_schedclock.time - gd->gd_schedclock.periodic;

	if (lp->lwp_slptime > 1) {
		/*
		 * Too much time has passed, do a coarse correction.
		 */
		lp->lwp_estcpu = lp->lwp_estcpu >> 1;
		dfly_resetpriority(lp);
		lp->lwp_cpbase = cpbase;
		lp->lwp_cpticks = 0;
		lp->lwp_batch -= ESTCPUFREQ;
		if (lp->lwp_batch < 0)
			lp->lwp_batch = 0;
	} else if (lp->lwp_cpbase != cpbase) {
		/*
		 * Adjust estcpu if we are in a different tick.  Don't waste
		 * time if we are in the same tick.
		 *
		 * First calculate the number of ticks in the measurement
		 * interval.  The ttlticks calculation can wind up 0 due to
		 * a bug in the handling of lwp_slptime  (as yet not found),
		 * so make sure we do not get a divide by 0 panic.
		 */
		ttlticks = (cpbase - lp->lwp_cpbase) /
			   gd->gd_schedclock.periodic;
		if (ttlticks < 0) {
			ttlticks = 0;
			lp->lwp_cpbase = cpbase;
		}
		if (ttlticks == 0)
			return;
		updatepcpu(lp, lp->lwp_cpticks, ttlticks);

		/*
		 * Calculate the percentage of one cpu used factoring in ncpus
		 * and the load and adjust estcpu.  Handle degenerate cases
		 * by adding 1 to runqcount.
		 *
		 * estcpu is scaled by ESTCPUMAX.
		 *
		 * runqcount is the excess number of user processes
		 * that cannot be immediately scheduled to cpus.  We want
		 * to count these as running to avoid range compression
		 * in the base calculation (which is the actual percentage
		 * of one cpu used).
		 */
		estcpu = (lp->lwp_cpticks * ESTCPUMAX) *
			 (dd->runqcount + ncpus) / (ncpus * ttlticks);

		/*
		 * If estcpu is > 50% we become more batch-like
		 * If estcpu is <= 50% we become less batch-like
		 *
		 * It takes 30 cpu seconds to traverse the entire range.
		 */
		if (estcpu > ESTCPUMAX / 2) {
			lp->lwp_batch += ttlticks;
			if (lp->lwp_batch > BATCHMAX)
				lp->lwp_batch = BATCHMAX;
		} else {
			lp->lwp_batch -= ttlticks;
			if (lp->lwp_batch < 0)
				lp->lwp_batch = 0;
		}

		if (usched_dfly_debug == lp->lwp_proc->p_pid) {
			kprintf("pid %d lwp %p estcpu %3d %3d bat %d cp %d/%d",
				lp->lwp_proc->p_pid, lp,
				estcpu, lp->lwp_estcpu,
				lp->lwp_batch,
				lp->lwp_cpticks, ttlticks);
		}

		/*
		 * Adjust lp->lwp_esetcpu.  The decay factor determines how
		 * quickly lwp_estcpu collapses to its realtime calculation.
		 * A slower collapse gives us a more accurate number but
		 * can cause a cpu hog to eat too much cpu before the
		 * scheduler decides to downgrade it.
		 *
		 * NOTE: p_nice is accounted for in dfly_resetpriority(),
		 *	 and not here, but we must still ensure that a
		 *	 cpu-bound nice -20 process does not completely
		 *	 override a cpu-bound nice +20 process.
		 *
		 * NOTE: We must use ESTCPULIM() here to deal with any
		 *	 overshoot.
		 */
		decay_factor = usched_dfly_decay;
		if (decay_factor < 1)
			decay_factor = 1;
		if (decay_factor > 1024)
			decay_factor = 1024;

		lp->lwp_estcpu = ESTCPULIM(
			(lp->lwp_estcpu * decay_factor + estcpu) /
			(decay_factor + 1));

		if (usched_dfly_debug == lp->lwp_proc->p_pid)
			kprintf(" finalestcpu %d\n", lp->lwp_estcpu);
		dfly_resetpriority(lp);
		lp->lwp_cpbase += ttlticks * gd->gd_schedclock.periodic;
		lp->lwp_cpticks = 0;
	}
}

/*
 * Compute the priority of a process when running in user mode.
 * Arrange to reschedule if the resulting priority is better
 * than that of the current process.
 *
 * This routine may be called with any process.
 *
 * This routine is called by fork1() for initial setup with the process
 * of the run queue, and also may be called normally with the process on or
 * off the run queue.
 */
static void
dfly_resetpriority(struct lwp *lp)
{
	dfly_pcpu_t rdd;
	int newpriority;
	u_short newrqtype;
	int rcpu;
	int checkpri;
	int estcpu;

	crit_enter();

	/*
	 * Lock the scheduler (lp) belongs to.  This can be on a different
	 * cpu.  Handle races.  This loop breaks out with the appropriate
	 * rdd locked.
	 */
	for (;;) {
		rcpu = lp->lwp_qcpu;
		cpu_ccfence();
		rdd = &dfly_pcpu[rcpu];
		spin_lock(&rdd->spin);
		if (rcpu == lp->lwp_qcpu)
			break;
		spin_unlock(&rdd->spin);
	}

	/*
	 * Calculate the new priority and queue type
	 */
	newrqtype = lp->lwp_rtprio.type;

	switch(newrqtype) {
	case RTP_PRIO_REALTIME:
	case RTP_PRIO_FIFO:
		newpriority = PRIBASE_REALTIME +
			     (lp->lwp_rtprio.prio & PRIMASK);
		break;
	case RTP_PRIO_NORMAL:
		/*
		 * Detune estcpu based on batchiness.  lwp_batch ranges
		 * from 0 to  BATCHMAX.  Limit estcpu for the sake of
		 * the priority calculation to between 50% and 100%.
		 */
		estcpu = lp->lwp_estcpu * (lp->lwp_batch + BATCHMAX) /
			 (BATCHMAX * 2);

		/*
		 * p_nice piece		Adds (0-40) * 2		0-80
		 * estcpu		Adds 16384  * 4 / 512   0-128
		 */
		newpriority = (lp->lwp_proc->p_nice - PRIO_MIN) * PPQ / NICEPPQ;
		newpriority += estcpu * PPQ / ESTCPUPPQ;
		newpriority = newpriority * MAXPRI / (PRIO_RANGE * PPQ /
			      NICEPPQ + ESTCPUMAX * PPQ / ESTCPUPPQ);
		newpriority = PRIBASE_NORMAL + (newpriority & PRIMASK);
		break;
	case RTP_PRIO_IDLE:
		newpriority = PRIBASE_IDLE + (lp->lwp_rtprio.prio & PRIMASK);
		break;
	case RTP_PRIO_THREAD:
		newpriority = PRIBASE_THREAD + (lp->lwp_rtprio.prio & PRIMASK);
		break;
	default:
		panic("Bad RTP_PRIO %d", newrqtype);
		/* NOT REACHED */
	}

	/*
	 * The newpriority incorporates the queue type so do a simple masked
	 * check to determine if the process has moved to another queue.  If
	 * it has, and it is currently on a run queue, then move it.
	 *
	 * Since uload is ~PPQMASK masked, no modifications are necessary if
	 * we end up in the same run queue.
	 */
	if ((lp->lwp_priority ^ newpriority) & ~PPQMASK) {
		int delta_uload;

		/*
		 * uload can change, calculate the adjustment to reduce
		 * edge cases since choosers scan the cpu topology without
		 * locks.
		 */
		if (lp->lwp_mpflags & LWP_MP_ULOAD) {
			delta_uload =
				-((lp->lwp_priority & ~PPQMASK) & PRIMASK) +
				((newpriority & ~PPQMASK) & PRIMASK);
			atomic_add_int(&dfly_pcpu[lp->lwp_qcpu].uload,
				       delta_uload);
		}
		if (lp->lwp_mpflags & LWP_MP_ONRUNQ) {
			dfly_remrunqueue_locked(rdd, lp);
			lp->lwp_priority = newpriority;
			lp->lwp_rqtype = newrqtype;
			lp->lwp_rqindex = (newpriority & PRIMASK) / PPQ;
			dfly_setrunqueue_locked(rdd, lp);
			checkpri = 1;
		} else {
			lp->lwp_priority = newpriority;
			lp->lwp_rqtype = newrqtype;
			lp->lwp_rqindex = (newpriority & PRIMASK) / PPQ;
			checkpri = 0;
		}
	} else {
		/*
		 * In the same PPQ, uload cannot change.
		 */
		lp->lwp_priority = newpriority;
		checkpri = 1;
		rcpu = -1;
	}

	/*
	 * Determine if we need to reschedule the target cpu.  This only
	 * occurs if the LWP is already on a scheduler queue, which means
	 * that idle cpu notification has already occured.  At most we
	 * need only issue a need_user_resched() on the appropriate cpu.
	 *
	 * The LWP may be owned by a CPU different from the current one,
	 * in which case dd->uschedcp may be modified without an MP lock
	 * or a spinlock held.  The worst that happens is that the code
	 * below causes a spurious need_user_resched() on the target CPU
	 * and dd->pri to be wrong for a short period of time, both of
	 * which are harmless.
	 *
	 * If checkpri is 0 we are adjusting the priority of the current
	 * process, possibly higher (less desireable), so ignore the upri
	 * check which will fail in that case.
	 */
	if (rcpu >= 0) {
		if ((dfly_rdyprocmask & CPUMASK(rcpu)) &&
		    (checkpri == 0 ||
		     (rdd->upri & ~PRIMASK) > (lp->lwp_priority & ~PRIMASK))) {
#ifdef SMP
			if (rcpu == mycpu->gd_cpuid) {
				spin_unlock(&rdd->spin);
				need_user_resched();
			} else {
				atomic_clear_cpumask(&dfly_rdyprocmask,
						     CPUMASK(rcpu));
				spin_unlock(&rdd->spin);
				lwkt_send_ipiq(globaldata_find(rcpu),
					       dfly_need_user_resched_remote,
					       NULL);
			}
#else
			spin_unlock(&rdd->spin);
			need_user_resched();
#endif
		} else {
			spin_unlock(&rdd->spin);
		}
	} else {
		spin_unlock(&rdd->spin);
	}
	crit_exit();
}

static
void
dfly_yield(struct lwp *lp)
{
#if 0
	/* FUTURE (or something similar) */
	switch(lp->lwp_rqtype) {
	case RTP_PRIO_NORMAL:
		lp->lwp_estcpu = ESTCPULIM(lp->lwp_estcpu + ESTCPUINCR);
		break;
	default:
		break;
	}
#endif
        need_user_resched();
}

/*
 * Called from fork1() when a new child process is being created.
 *
 * Give the child process an initial estcpu that is more batch then
 * its parent and dock the parent for the fork (but do not
 * reschedule the parent).   This comprises the main part of our batch
 * detection heuristic for both parallel forking and sequential execs.
 *
 * XXX lwp should be "spawning" instead of "forking"
 */
static void
dfly_forking(struct lwp *plp, struct lwp *lp)
{
	/*
	 * Put the child 4 queue slots (out of 32) higher than the parent
	 * (less desireable than the parent).
	 */
	lp->lwp_estcpu = ESTCPULIM(plp->lwp_estcpu + ESTCPUPPQ * 4);
	lp->lwp_forked = 1;

	/*
	 * The batch status of children always starts out centerline
	 * and will inch-up or inch-down as appropriate.  It takes roughly
	 * ~15 seconds of >50% cpu to hit the limit.
	 */
	lp->lwp_batch = BATCHMAX / 2;

	/*
	 * Dock the parent a cost for the fork, protecting us from fork
	 * bombs.  If the parent is forking quickly make the child more
	 * batchy.
	 */
	plp->lwp_estcpu = ESTCPULIM(plp->lwp_estcpu + ESTCPUPPQ / 16);
}

/*
 * Called when a lwp is being removed from this scheduler, typically
 * during lwp_exit().
 */
static void
dfly_exiting(struct lwp *lp, struct proc *child_proc)
{
	dfly_pcpu_t dd = &dfly_pcpu[lp->lwp_qcpu];

	if (lp->lwp_mpflags & LWP_MP_ULOAD) {
		atomic_clear_int(&lp->lwp_mpflags, LWP_MP_ULOAD);
		atomic_add_int(&dd->uload,
			       -((lp->lwp_priority & ~PPQMASK) & PRIMASK));

		/*
		 * The uload might have stopped the scheduler helper from
		 * pulling in a process from another cpu, so kick it now
		 * if we have to.
		 */
		if (dd->uschedcp == NULL &&
		    (dfly_rdyprocmask & dd->cpumask) &&
		    (usched_dfly_features & 0x01) &&
		    ((usched_dfly_features & 0x02) == 0 ||
		     dd->uload < MAXPRI / 4))
		{
			atomic_clear_cpumask(&dfly_rdyprocmask, dd->cpumask);
			wakeup(&dd->helper_thread);
		}
	}
}

/*
 * This function cannot block in any way
 */
static void
dfly_uload_update(struct lwp *lp)
{
	dfly_pcpu_t dd = &dfly_pcpu[lp->lwp_qcpu];

	if (lp->lwp_thread->td_flags & TDF_RUNQ) {
		if ((lp->lwp_mpflags & LWP_MP_ULOAD) == 0) {
			atomic_set_int(&lp->lwp_mpflags, LWP_MP_ULOAD);
			atomic_add_int(&dd->uload,
				   ((lp->lwp_priority & ~PPQMASK) & PRIMASK));
		}
	} else {
		if (lp->lwp_mpflags & LWP_MP_ULOAD) {
			atomic_clear_int(&lp->lwp_mpflags, LWP_MP_ULOAD);
			atomic_add_int(&dd->uload,
				   -((lp->lwp_priority & ~PPQMASK) & PRIMASK));
		}
	}
}

/*
 * chooseproc() is called when a cpu needs a user process to LWKT schedule,
 * it selects a user process and returns it.  If chklp is non-NULL and chklp
 * has a better or equal priority then the process that would otherwise be
 * chosen, NULL is returned.
 *
 * Until we fix the RUNQ code the chklp test has to be strict or we may
 * bounce between processes trying to acquire the current process designation.
 *
 * Must be called with rdd->spin locked.  The spinlock is left intact through
 * the entire routine.  dd->spin does not have to be locked.
 *
 * If worst is non-zero this function finds the worst thread instead of the
 * best thread (used by the schedulerclock-based rover).
 */
static
struct lwp *
dfly_chooseproc_locked(dfly_pcpu_t rdd, dfly_pcpu_t dd,
		       struct lwp *chklp, int worst)
{
	struct lwp *lp;
	struct rq *q;
	u_int32_t *which, *which2;
	u_int32_t pri;
	u_int32_t rtqbits;
	u_int32_t tsqbits;
	u_int32_t idqbits;

	rtqbits = rdd->rtqueuebits;
	tsqbits = rdd->queuebits;
	idqbits = rdd->idqueuebits;

	if (worst) {
		if (idqbits) {
			pri = bsfl(idqbits);
			q = &rdd->idqueues[pri];
			which = &rdd->idqueuebits;
			which2 = &idqbits;
		} else if (tsqbits) {
			pri = bsfl(tsqbits);
			q = &rdd->queues[pri];
			which = &rdd->queuebits;
			which2 = &tsqbits;
		} else if (rtqbits) {
			pri = bsfl(rtqbits);
			q = &rdd->rtqueues[pri];
			which = &rdd->rtqueuebits;
			which2 = &rtqbits;
		} else {
			return (NULL);
		}
		lp = TAILQ_LAST(q, rq);
	} else {
		if (rtqbits) {
			pri = bsfl(rtqbits);
			q = &rdd->rtqueues[pri];
			which = &rdd->rtqueuebits;
			which2 = &rtqbits;
		} else if (tsqbits) {
			pri = bsfl(tsqbits);
			q = &rdd->queues[pri];
			which = &rdd->queuebits;
			which2 = &tsqbits;
		} else if (idqbits) {
			pri = bsfl(idqbits);
			q = &rdd->idqueues[pri];
			which = &rdd->idqueuebits;
			which2 = &idqbits;
		} else {
			return (NULL);
		}
		lp = TAILQ_FIRST(q);
	}
	KASSERT(lp, ("chooseproc: no lwp on busy queue"));

	/*
	 * If the passed lwp <chklp> is reasonably close to the selected
	 * lwp <lp>, return NULL (indicating that <chklp> should be kept).
	 *
	 * Note that we must error on the side of <chklp> to avoid bouncing
	 * between threads in the acquire code.
	 */
	if (chklp) {
		if (chklp->lwp_priority < lp->lwp_priority + PPQ)
			return(NULL);
	}

	KTR_COND_LOG(usched_chooseproc,
	    lp->lwp_proc->p_pid == usched_dfly_pid_debug,
	    lp->lwp_proc->p_pid,
	    lp->lwp_thread->td_gd->gd_cpuid,
	    mycpu->gd_cpuid);

	KASSERT((lp->lwp_mpflags & LWP_MP_ONRUNQ) != 0, ("not on runq6!"));
	atomic_clear_int(&lp->lwp_mpflags, LWP_MP_ONRUNQ);
	TAILQ_REMOVE(q, lp, lwp_procq);
	--rdd->runqcount;
	if (TAILQ_EMPTY(q))
		*which &= ~(1 << pri);

	if (rdd != dd) {
		if (lp->lwp_mpflags & LWP_MP_ULOAD) {
			atomic_add_int(&rdd->uload,
			    -((lp->lwp_priority & ~PPQMASK) & PRIMASK));
		}
		lp->lwp_qcpu = dd->cpuid;
		atomic_add_int(&dd->uload,
		    ((lp->lwp_priority & ~PPQMASK) & PRIMASK));
		atomic_set_int(&lp->lwp_mpflags, LWP_MP_ULOAD);
	}
	return lp;
}

#ifdef SMP

/*
 * USED TO PUSH RUNNABLE LWPS TO THE LEAST LOADED CPU.
 *
 * Choose a cpu node to schedule lp on, hopefully nearby its current
 * node.  We give the current node a modest advantage for obvious reasons.
 *
 * We also give the node the thread was woken up FROM a slight advantage
 * in order to try to schedule paired threads which synchronize/block waiting
 * for each other fairly close to each other.  Similarly in a network setting
 * this feature will also attempt to place a user process near the kernel
 * protocol thread that is feeding it data.  THIS IS A CRITICAL PART of the
 * algorithm as it heuristically groups synchronizing processes for locality
 * of reference in multi-socket systems.
 *
 * The caller will normally dfly_setrunqueue() lp on the returned queue.
 *
 * When the topology is known choose a cpu whos group has, in aggregate,
 * has the lowest weighted load.
 */
static
dfly_pcpu_t
dfly_choose_best_queue(struct lwp *lp)
{
	cpumask_t mask;
	cpu_node_t *cpup;
	cpu_node_t *cpun;
	cpu_node_t *cpub;
	dfly_pcpu_t dd1 = &dfly_pcpu[lp->lwp_qcpu];
	dfly_pcpu_t dd2 = &dfly_pcpu[lp->lwp_thread->td_wakefromcpu];
	dfly_pcpu_t rdd;
	int cpuid;
	int n;
	int count;
	int load;
	int lowest_load;

	/*
	 * When the topology is unknown choose a random cpu that is hopefully
	 * idle.
	 */
	if (dd1->cpunode == NULL)
		return (dfly_choose_queue_simple(dd1, lp));

	/*
	 * When the topology is known choose a cpu whos group has, in
	 * aggregate, has the lowest weighted load.
	 */
	cpup = root_cpu_node;
	rdd = dd1;

	while (cpup) {
		/*
		 * Degenerate case super-root
		 */
		if (cpup->child_node && cpup->child_no == 1) {
			cpup = cpup->child_node;
			continue;
		}

		/*
		 * Terminal cpunode
		 */
		if (cpup->child_node == NULL) {
			rdd = &dfly_pcpu[BSFCPUMASK(cpup->members)];
			break;
		}

		cpub = NULL;
		lowest_load = 0x7FFFFFFF;

		for (n = 0; n < cpup->child_no; ++n) {
			/*
			 * Accumulate load information for all cpus
			 * which are members of this node.
			 */
			cpun = &cpup->child_node[n];
			mask = cpun->members & usched_global_cpumask &
			       smp_active_mask & lp->lwp_cpumask;
			if (mask == 0)
				continue;

			/*
			 * Compensate if the lp is already accounted for in
			 * the aggregate uload for this mask set.  We want
			 * to calculate the loads as if lp was not present.
			 */
			if ((lp->lwp_mpflags & LWP_MP_ULOAD) &&
			    CPUMASK(lp->lwp_qcpu) & mask) {
				load = -((lp->lwp_priority & ~PPQMASK) &
				         PRIMASK);
			} else {
				load = 0;
			}

			count = 0;
			while (mask) {
				cpuid = BSFCPUMASK(mask);
				load += dfly_pcpu[cpuid].uload;
				load += dfly_pcpu[cpuid].runqcount *
					usched_dfly_weight3;
				mask &= ~CPUMASK(cpuid);
				++count;
			}
			load /= count;

			/*
			 * Give a slight advantage to the cpu groups (lp)
			 * belongs to.
			 *
			 * Give a slight advantage to the cpu groups our
			 * synchronous partner belongs to.  However, to
			 * avoid flapping in a two-way comm environment
			 * we only employ this measure when the wake-from's
			 * cpu is higher than lp's cpu.
			 */
			if (cpun->members & dd1->cpumask)
				load -= usched_dfly_weight1;
			else if ((cpun->members & dd2->cpumask) && dd1 < dd2)
				load -= usched_dfly_weight2;

			/*
			 * Calculate the best load
			 */
			if (cpub == NULL || lowest_load > load ||
			    (lowest_load == load &&
			     (cpun->members & dd1->cpumask))
			) {
				lowest_load = load;
				cpub = cpun;
			}
		}
		cpup = cpub;
	}
	if (usched_dfly_chooser)
		kprintf("lp %02d->%02d %s\n",
			lp->lwp_qcpu, rdd->cpuid, lp->lwp_proc->p_comm);
	return (rdd);
}

/*
 * USED TO PULL RUNNABLE LWPS FROM THE MOST LOADED CPU.
 *
 * Choose the worst queue close to dd's cpu node with a non-empty runq
 * that is NOT dd.
 *
 * This is used by the thread chooser when the current cpu's queues are
 * empty to steal a thread from another cpu's queue.  We want to offload
 * the most heavily-loaded queue.
 */
static
dfly_pcpu_t
dfly_choose_worst_queue(dfly_pcpu_t dd, int weight)
{
	cpumask_t mask;
	cpu_node_t *cpup;
	cpu_node_t *cpun;
	cpu_node_t *cpub;
	dfly_pcpu_t rdd;
	int cpuid;
	int n;
	int count;
	int load;
	int highest_load;

	/*
	 * When the topology is unknown choose a random cpu that is hopefully
	 * idle.
	 */
	if (dd->cpunode == NULL) {
		return (NULL);
	}

	/*
	 * When the topology is known choose a cpu whos group has, in
	 * aggregate, has the lowest weighted load.
	 */
	cpup = root_cpu_node;
	rdd = dd;
	while (cpup) {
		/*
		 * Degenerate case super-root
		 */
		if (cpup->child_node && cpup->child_no == 1) {
			cpup = cpup->child_node;
			continue;
		}

		/*
		 * Terminal cpunode
		 */
		if (cpup->child_node == NULL) {
			rdd = &dfly_pcpu[BSFCPUMASK(cpup->members)];
			break;
		}

		cpub = NULL;
		highest_load = 0;

		for (n = 0; n < cpup->child_no; ++n) {
			/*
			 * Accumulate load information for all cpus
			 * which are members of this node.
			 */
			cpun = &cpup->child_node[n];
			mask = cpun->members & usched_global_cpumask &
			       smp_active_mask;
			if (mask == 0)
				continue;
			count = 0;
			load = 0;
			while (mask) {
				cpuid = BSFCPUMASK(mask);
				load += dfly_pcpu[cpuid].uload;
				load += dfly_pcpu[cpuid].runqcount *
					usched_dfly_weight3;
				mask &= ~CPUMASK(cpuid);
				++count;
			}
			load /= count;

			/*
			 * Prefer candidates which are somewhat closer to
			 * our cpu.
			 */
			if (dd->cpumask & cpun->members)
				load += usched_dfly_weight1;

			/*
			 * The best candidate is the one with the worst
			 * (highest) load.
			 */
			if (cpub == NULL || highest_load < load) {
				highest_load = load;
				cpub = cpun;
			}
		}
		cpup = cpub;
	}

	/*
	 * We never return our own node (dd), and only return a remote
	 * node if it's load is significantly worse than ours (i.e. where
	 * stealing a thread would be considered reasonable).
	 *
	 * This also helps us avoid breaking paired threads apart which
	 * can have disastrous effects on performance.
	 */
	if (rdd == dd || rdd->uload < dd->uload + weight)
		rdd = NULL;
	return (rdd);
}

static
dfly_pcpu_t
dfly_choose_queue_simple(dfly_pcpu_t dd, struct lwp *lp)
{
	dfly_pcpu_t rdd;
	cpumask_t tmpmask;
	cpumask_t mask;
	int cpuid;

	/*
	 * Fallback to the original heuristic, select random cpu,
	 * first checking cpus not currently running a user thread.
	 */
	++dfly_scancpu;
	cpuid = (dfly_scancpu & 0xFFFF) % ncpus;
	mask = ~dfly_curprocmask & dfly_rdyprocmask & lp->lwp_cpumask &
	       smp_active_mask & usched_global_cpumask;

	while (mask) {
		tmpmask = ~(CPUMASK(cpuid) - 1);
		if (mask & tmpmask)
			cpuid = BSFCPUMASK(mask & tmpmask);
		else
			cpuid = BSFCPUMASK(mask);
		rdd = &dfly_pcpu[cpuid];

		if ((rdd->upri & ~PPQMASK) >= (lp->lwp_priority & ~PPQMASK))
			goto found;
		mask &= ~CPUMASK(cpuid);
	}

	/*
	 * Then cpus which might have a currently running lp
	 */
	cpuid = (dfly_scancpu & 0xFFFF) % ncpus;
	mask = dfly_curprocmask & dfly_rdyprocmask &
	       lp->lwp_cpumask & smp_active_mask & usched_global_cpumask;

	while (mask) {
		tmpmask = ~(CPUMASK(cpuid) - 1);
		if (mask & tmpmask)
			cpuid = BSFCPUMASK(mask & tmpmask);
		else
			cpuid = BSFCPUMASK(mask);
		rdd = &dfly_pcpu[cpuid];

		if ((rdd->upri & ~PPQMASK) > (lp->lwp_priority & ~PPQMASK))
			goto found;
		mask &= ~CPUMASK(cpuid);
	}

	/*
	 * If we cannot find a suitable cpu we reload from dfly_scancpu
	 * and round-robin.  Other cpus will pickup as they release their
	 * current lwps or become ready.
	 *
	 * Avoid a degenerate system lockup case if usched_global_cpumask
	 * is set to 0 or otherwise does not cover lwp_cpumask.
	 *
	 * We only kick the target helper thread in this case, we do not
	 * set the user resched flag because
	 */
	cpuid = (dfly_scancpu & 0xFFFF) % ncpus;
	if ((CPUMASK(cpuid) & usched_global_cpumask) == 0)
		cpuid = 0;
	rdd = &dfly_pcpu[cpuid];
found:
	return (rdd);
}

static
void
dfly_need_user_resched_remote(void *dummy)
{
	globaldata_t gd = mycpu;
	dfly_pcpu_t  dd = &dfly_pcpu[gd->gd_cpuid];

	need_user_resched();

	/* Call wakeup_mycpu to avoid sending IPIs to other CPUs */
	wakeup_mycpu(&dd->helper_thread);
}

#endif

/*
 * dfly_remrunqueue_locked() removes a given process from the run queue
 * that it is on, clearing the queue busy bit if it becomes empty.
 *
 * Note that user process scheduler is different from the LWKT schedule.
 * The user process scheduler only manages user processes but it uses LWKT
 * underneath, and a user process operating in the kernel will often be
 * 'released' from our management.
 *
 * uload is NOT adjusted here.  It is only adjusted if the lwkt_thread goes
 * to sleep or the lwp is moved to a different runq.
 */
static void
dfly_remrunqueue_locked(dfly_pcpu_t rdd, struct lwp *lp)
{
	struct rq *q;
	u_int32_t *which;
	u_int8_t pri;

	KKASSERT(rdd->runqcount >= 0);

	pri = lp->lwp_rqindex;
	switch(lp->lwp_rqtype) {
	case RTP_PRIO_NORMAL:
		q = &rdd->queues[pri];
		which = &rdd->queuebits;
		break;
	case RTP_PRIO_REALTIME:
	case RTP_PRIO_FIFO:
		q = &rdd->rtqueues[pri];
		which = &rdd->rtqueuebits;
		break;
	case RTP_PRIO_IDLE:
		q = &rdd->idqueues[pri];
		which = &rdd->idqueuebits;
		break;
	default:
		panic("remrunqueue: invalid rtprio type");
		/* NOT REACHED */
	}
	KKASSERT(lp->lwp_mpflags & LWP_MP_ONRUNQ);
	atomic_clear_int(&lp->lwp_mpflags, LWP_MP_ONRUNQ);
	TAILQ_REMOVE(q, lp, lwp_procq);
	--rdd->runqcount;
	if (TAILQ_EMPTY(q)) {
		KASSERT((*which & (1 << pri)) != 0,
			("remrunqueue: remove from empty queue"));
		*which &= ~(1 << pri);
	}
}

/*
 * dfly_setrunqueue_locked()
 *
 * Add a process whos rqtype and rqindex had previously been calculated
 * onto the appropriate run queue.   Determine if the addition requires
 * a reschedule on a cpu and return the cpuid or -1.
 *
 * NOTE: 	  Lower priorities are better priorities.
 *
 * NOTE ON ULOAD: This variable specifies the aggregate load on a cpu, the
 *		  sum of the rough lwp_priority for all running and runnable
 *		  processes.  Lower priority processes (higher lwp_priority
 *		  values) actually DO count as more load, not less, because
 *		  these are the programs which require the most care with
 *		  regards to cpu selection.
 */
static void
dfly_setrunqueue_locked(dfly_pcpu_t rdd, struct lwp *lp)
{
	struct rq *q;
	u_int32_t *which;
	int pri;

	if (lp->lwp_qcpu != rdd->cpuid) {
		if (lp->lwp_mpflags & LWP_MP_ULOAD) {
			atomic_clear_int(&lp->lwp_mpflags, LWP_MP_ULOAD);
			atomic_add_int(&dfly_pcpu[lp->lwp_qcpu].uload,
				   -((lp->lwp_priority & ~PPQMASK) & PRIMASK));
		}
		lp->lwp_qcpu = rdd->cpuid;
	}

	if ((lp->lwp_mpflags & LWP_MP_ULOAD) == 0) {
		atomic_set_int(&lp->lwp_mpflags, LWP_MP_ULOAD);
		atomic_add_int(&dfly_pcpu[lp->lwp_qcpu].uload,
			       (lp->lwp_priority & ~PPQMASK) & PRIMASK);
	}

	pri = lp->lwp_rqindex;

	switch(lp->lwp_rqtype) {
	case RTP_PRIO_NORMAL:
		q = &rdd->queues[pri];
		which = &rdd->queuebits;
		break;
	case RTP_PRIO_REALTIME:
	case RTP_PRIO_FIFO:
		q = &rdd->rtqueues[pri];
		which = &rdd->rtqueuebits;
		break;
	case RTP_PRIO_IDLE:
		q = &rdd->idqueues[pri];
		which = &rdd->idqueuebits;
		break;
	default:
		panic("remrunqueue: invalid rtprio type");
		/* NOT REACHED */
	}

	/*
	 * Add to the correct queue and set the appropriate bit.  If no
	 * lower priority (i.e. better) processes are in the queue then
	 * we want a reschedule, calculate the best cpu for the job.
	 *
	 * Always run reschedules on the LWPs original cpu.
	 */
	KKASSERT((lp->lwp_mpflags & LWP_MP_ONRUNQ) == 0);
	atomic_set_int(&lp->lwp_mpflags, LWP_MP_ONRUNQ);
	++rdd->runqcount;
	TAILQ_INSERT_TAIL(q, lp, lwp_procq);
	*which |= 1 << pri;
}

#ifdef SMP

/*
 * For SMP systems a user scheduler helper thread is created for each
 * cpu and is used to allow one cpu to wakeup another for the purposes of
 * scheduling userland threads from setrunqueue().
 *
 * UP systems do not need the helper since there is only one cpu.
 *
 * We can't use the idle thread for this because we might block.
 * Additionally, doing things this way allows us to HLT idle cpus
 * on MP systems.
 */
static void
dfly_helper_thread(void *dummy)
{
    globaldata_t gd;
    dfly_pcpu_t dd;
    dfly_pcpu_t rdd;
    struct lwp *nlp;
    cpumask_t mask;
    int cpuid;

    gd = mycpu;
    cpuid = gd->gd_cpuid;	/* doesn't change */
    mask = gd->gd_cpumask;	/* doesn't change */
    dd = &dfly_pcpu[cpuid];

    /*
     * Since we only want to be woken up only when no user processes
     * are scheduled on a cpu, run at an ultra low priority.
     */
    lwkt_setpri_self(TDPRI_USER_SCHEDULER);

    tsleep(&dd->helper_thread, 0, "schslp", 0);

    for (;;) {
	/*
	 * We use the LWKT deschedule-interlock trick to avoid racing
	 * dfly_rdyprocmask.  This means we cannot block through to the
	 * manual lwkt_switch() call we make below.
	 */
	crit_enter_gd(gd);
	tsleep_interlock(&dd->helper_thread, 0);

	spin_lock(&dd->spin);

	atomic_set_cpumask(&dfly_rdyprocmask, mask);
	clear_user_resched();	/* This satisfied the reschedule request */
	dd->rrcount = 0;	/* Reset the round-robin counter */

	if (dd->runqcount || dd->uschedcp != NULL) {
		/*
		 * Threads are available.  A thread may or may not be
		 * currently scheduled.  Get the best thread already queued
		 * to this cpu.
		 */
		nlp = dfly_chooseproc_locked(dd, dd, dd->uschedcp, 0);
		if (nlp) {
			atomic_set_cpumask(&dfly_curprocmask, mask);
			dd->upri = nlp->lwp_priority;
			dd->uschedcp = nlp;
			dd->rrcount = 0;	/* reset round robin */
			spin_unlock(&dd->spin);
			lwkt_acquire(nlp->lwp_thread);
			lwkt_schedule(nlp->lwp_thread);
		} else {
			/*
			 * This situation should not occur because we had
			 * at least one thread available.
			 */
			spin_unlock(&dd->spin);
		}
	} else if ((usched_dfly_features & 0x01) &&
		   ((usched_dfly_features & 0x02) == 0 ||
		    dd->uload < MAXPRI / 4)) {
		/*
		 * This cpu is devoid of runnable threads, steal a thread
		 * from another cpu.  Since we're stealing, might as well
		 * load balance at the same time.
		 *
		 * NOTE! This function only returns a non-NULL rdd when
		 *	 another cpu's queue is obviously overloaded.  We
		 *	 do not want to perform the type of rebalancing
		 *	 the schedclock does here because it would result
		 *	 in insane process pulling when 'steady' state is
		 *	 partially unbalanced (e.g. 6 runnables and only
		 *	 4 cores).
		 */
		rdd = dfly_choose_worst_queue(dd, usched_dfly_weight1 * 8);
		if (rdd && spin_trylock(&rdd->spin)) {
			nlp = dfly_chooseproc_locked(rdd, dd, NULL, 0);
			spin_unlock(&rdd->spin);
		} else {
			nlp = NULL;
		}
		if (nlp) {
			atomic_set_cpumask(&dfly_curprocmask, mask);
			dd->upri = nlp->lwp_priority;
			dd->uschedcp = nlp;
			dd->rrcount = 0;	/* reset round robin */
			spin_unlock(&dd->spin);
			lwkt_acquire(nlp->lwp_thread);
			lwkt_schedule(nlp->lwp_thread);
		} else {
			/*
			 * Leave the thread on our run queue.  Another
			 * scheduler will try to pull it later.
			 */
			spin_unlock(&dd->spin);
		}
	} else {
		/*
		 * devoid of runnable threads and not allowed to steal
		 * any.
		 */
		spin_unlock(&dd->spin);
	}

	/*
	 * We're descheduled unless someone scheduled us.  Switch away.
	 * Exiting the critical section will cause splz() to be called
	 * for us if interrupts and such are pending.
	 */
	crit_exit_gd(gd);
	tsleep(&dd->helper_thread, PINTERLOCKED, "schslp", 0);
    }
}

#if 0
static int
sysctl_usched_dfly_stick_to_level(SYSCTL_HANDLER_ARGS)
{
	int error, new_val;

	new_val = usched_dfly_stick_to_level;

	error = sysctl_handle_int(oidp, &new_val, 0, req);
        if (error != 0 || req->newptr == NULL)
		return (error);
	if (new_val > cpu_topology_levels_number - 1 || new_val < 0)
		return (EINVAL);
	usched_dfly_stick_to_level = new_val;
	return (0);
}
#endif

/*
 * Setup our scheduler helpers.  Note that curprocmask bit 0 has already
 * been cleared by rqinit() and we should not mess with it further.
 */
static void
dfly_helper_thread_cpu_init(void)
{
	int i;
	int j;
	int cpuid;
	int smt_not_supported = 0;
	int cache_coherent_not_supported = 0;

	if (bootverbose)
		kprintf("Start scheduler helpers on cpus:\n");

	sysctl_ctx_init(&usched_dfly_sysctl_ctx);
	usched_dfly_sysctl_tree =
		SYSCTL_ADD_NODE(&usched_dfly_sysctl_ctx,
				SYSCTL_STATIC_CHILDREN(_kern), OID_AUTO,
				"usched_dfly", CTLFLAG_RD, 0, "");

	for (i = 0; i < ncpus; ++i) {
		dfly_pcpu_t dd = &dfly_pcpu[i];
		cpumask_t mask = CPUMASK(i);

		if ((mask & smp_active_mask) == 0)
		    continue;

		spin_init(&dd->spin);
		dd->cpunode = get_cpu_node_by_cpuid(i);
		dd->cpuid = i;
		dd->cpumask = CPUMASK(i);
		for (j = 0; j < NQS; j++) {
			TAILQ_INIT(&dd->queues[j]);
			TAILQ_INIT(&dd->rtqueues[j]);
			TAILQ_INIT(&dd->idqueues[j]);
		}
		atomic_clear_cpumask(&dfly_curprocmask, 1);

		if (dd->cpunode == NULL) {
			smt_not_supported = 1;
			cache_coherent_not_supported = 1;
			if (bootverbose)
				kprintf ("\tcpu%d - WARNING: No CPU NODE "
					 "found for cpu\n", i);
		} else {
			switch (dd->cpunode->type) {
			case THREAD_LEVEL:
				if (bootverbose)
					kprintf ("\tcpu%d - HyperThreading "
						 "available. Core siblings: ",
						 i);
				break;
			case CORE_LEVEL:
				smt_not_supported = 1;

				if (bootverbose)
					kprintf ("\tcpu%d - No HT available, "
						 "multi-core/physical "
						 "cpu. Physical siblings: ",
						 i);
				break;
			case CHIP_LEVEL:
				smt_not_supported = 1;

				if (bootverbose)
					kprintf ("\tcpu%d - No HT available, "
						 "single-core/physical cpu. "
						 "Package Siblings: ",
						 i);
				break;
			default:
				/* Let's go for safe defaults here */
				smt_not_supported = 1;
				cache_coherent_not_supported = 1;
				if (bootverbose)
					kprintf ("\tcpu%d - Unknown cpunode->"
						 "type=%u. Siblings: ",
						 i,
						 (u_int)dd->cpunode->type);
				break;
			}

			if (bootverbose) {
				if (dd->cpunode->parent_node != NULL) {
					CPUSET_FOREACH(cpuid, dd->cpunode->parent_node->members)
						kprintf("cpu%d ", cpuid);
					kprintf("\n");
				} else {
					kprintf(" no siblings\n");
				}
			}
		}

		lwkt_create(dfly_helper_thread, NULL, NULL, &dd->helper_thread,
			    0, i, "usched %d", i);

		/*
		 * Allow user scheduling on the target cpu.  cpu #0 has already
		 * been enabled in rqinit().
		 */
		if (i)
		    atomic_clear_cpumask(&dfly_curprocmask, mask);
		atomic_set_cpumask(&dfly_rdyprocmask, mask);
		dd->upri = PRIBASE_NULL;

	}

	/* usched_dfly sysctl configurable parameters */

	SYSCTL_ADD_INT(&usched_dfly_sysctl_ctx,
		       SYSCTL_CHILDREN(usched_dfly_sysctl_tree),
		       OID_AUTO, "rrinterval", CTLFLAG_RW,
		       &usched_dfly_rrinterval, 0, "");
	SYSCTL_ADD_INT(&usched_dfly_sysctl_ctx,
		       SYSCTL_CHILDREN(usched_dfly_sysctl_tree),
		       OID_AUTO, "decay", CTLFLAG_RW,
		       &usched_dfly_decay, 0, "Extra decay when not running");
	SYSCTL_ADD_INT(&usched_dfly_sysctl_ctx,
		       SYSCTL_CHILDREN(usched_dfly_sysctl_tree),
		       OID_AUTO, "batch_time", CTLFLAG_RW,
		       &usched_dfly_batch_time, 0, "Min batch counter value");

	/* Add enable/disable option for SMT scheduling if supported */
	if (smt_not_supported) {
		usched_dfly_smt = 0;
		SYSCTL_ADD_STRING(&usched_dfly_sysctl_ctx,
				  SYSCTL_CHILDREN(usched_dfly_sysctl_tree),
				  OID_AUTO, "smt", CTLFLAG_RD,
				  "NOT SUPPORTED", 0, "SMT NOT SUPPORTED");
	} else {
		usched_dfly_smt = 1;
		SYSCTL_ADD_INT(&usched_dfly_sysctl_ctx,
			       SYSCTL_CHILDREN(usched_dfly_sysctl_tree),
			       OID_AUTO, "smt", CTLFLAG_RW,
			       &usched_dfly_smt, 0, "Enable SMT scheduling");
	}

	/*
	 * Add enable/disable option for cache coherent scheduling
	 * if supported
	 */
	if (cache_coherent_not_supported) {
		usched_dfly_cache_coherent = 0;
		SYSCTL_ADD_STRING(&usched_dfly_sysctl_ctx,
				  SYSCTL_CHILDREN(usched_dfly_sysctl_tree),
				  OID_AUTO, "cache_coherent", CTLFLAG_RD,
				  "NOT SUPPORTED", 0,
				  "Cache coherence NOT SUPPORTED");
	} else {
		usched_dfly_cache_coherent = 1;
		SYSCTL_ADD_INT(&usched_dfly_sysctl_ctx,
			       SYSCTL_CHILDREN(usched_dfly_sysctl_tree),
			       OID_AUTO, "cache_coherent", CTLFLAG_RW,
			       &usched_dfly_cache_coherent, 0,
			       "Enable/Disable cache coherent scheduling");

		SYSCTL_ADD_INT(&usched_dfly_sysctl_ctx,
			       SYSCTL_CHILDREN(usched_dfly_sysctl_tree),
			       OID_AUTO, "weight1", CTLFLAG_RW,
			       &usched_dfly_weight1, 10,
			       "Weight selection for current cpu");

		SYSCTL_ADD_INT(&usched_dfly_sysctl_ctx,
			       SYSCTL_CHILDREN(usched_dfly_sysctl_tree),
			       OID_AUTO, "weight2", CTLFLAG_RW,
			       &usched_dfly_weight2, 5,
			       "Weight selection for wakefrom cpu");

		SYSCTL_ADD_INT(&usched_dfly_sysctl_ctx,
			       SYSCTL_CHILDREN(usched_dfly_sysctl_tree),
			       OID_AUTO, "weight3", CTLFLAG_RW,
			       &usched_dfly_weight3, 50,
			       "Weight selection for num threads on queue");

		SYSCTL_ADD_INT(&usched_dfly_sysctl_ctx,
			       SYSCTL_CHILDREN(usched_dfly_sysctl_tree),
			       OID_AUTO, "features", CTLFLAG_RW,
			       &usched_dfly_features, 15,
			       "Allow pulls into empty queues");


#if 0
		SYSCTL_ADD_PROC(&usched_dfly_sysctl_ctx,
				SYSCTL_CHILDREN(usched_dfly_sysctl_tree),
				OID_AUTO, "stick_to_level",
				CTLTYPE_INT | CTLFLAG_RW,
				NULL, sizeof usched_dfly_stick_to_level,
				sysctl_usched_dfly_stick_to_level, "I",
				"Stick a process to this level. See sysctl"
				"paremter hw.cpu_topology.level_description");
#endif
	}
}
SYSINIT(uschedtd, SI_BOOT2_USCHED, SI_ORDER_SECOND,
	dfly_helper_thread_cpu_init, NULL)

#else /* No SMP options - just add the configurable parameters to sysctl */

static void
sched_sysctl_tree_init(void)
{
	sysctl_ctx_init(&usched_dfly_sysctl_ctx);
	usched_dfly_sysctl_tree =
		SYSCTL_ADD_NODE(&usched_dfly_sysctl_ctx,
				SYSCTL_STATIC_CHILDREN(_kern), OID_AUTO,
				"usched_dfly", CTLFLAG_RD, 0, "");

	/* usched_dfly sysctl configurable parameters */
	SYSCTL_ADD_INT(&usched_dfly_sysctl_ctx,
		       SYSCTL_CHILDREN(usched_dfly_sysctl_tree),
		       OID_AUTO, "rrinterval", CTLFLAG_RW,
		       &usched_dfly_rrinterval, 0, "");
	SYSCTL_ADD_INT(&usched_dfly_sysctl_ctx,
		       SYSCTL_CHILDREN(usched_dfly_sysctl_tree),
		       OID_AUTO, "decay", CTLFLAG_RW,
		       &usched_dfly_decay, 0, "Extra decay when not running");
	SYSCTL_ADD_INT(&usched_dfly_sysctl_ctx,
		       SYSCTL_CHILDREN(usched_dfly_sysctl_tree),
		       OID_AUTO, "batch_time", CTLFLAG_RW,
		       &usched_dfly_batch_time, 0, "Min batch counter value");
}
SYSINIT(uschedtd, SI_BOOT2_USCHED, SI_ORDER_SECOND,
	sched_sysctl_tree_init, NULL)
#endif
