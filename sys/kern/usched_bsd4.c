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
 *
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

#define lwp_priority	lwp_usdata.bsd4.priority
#define lwp_rqindex	lwp_usdata.bsd4.rqindex
#define lwp_estcpu	lwp_usdata.bsd4.estcpu
#define lwp_batch	lwp_usdata.bsd4.batch
#define lwp_rqtype	lwp_usdata.bsd4.rqtype

static void bsd4_acquire_curproc(struct lwp *lp);
static void bsd4_release_curproc(struct lwp *lp);
static void bsd4_select_curproc(globaldata_t gd);
static void bsd4_setrunqueue(struct lwp *lp);
static void bsd4_schedulerclock(struct lwp *lp, sysclock_t period,
				sysclock_t cpstamp);
static void bsd4_recalculate_estcpu(struct lwp *lp);
static void bsd4_resetpriority(struct lwp *lp);
static void bsd4_forking(struct lwp *plp, struct lwp *lp);
static void bsd4_exiting(struct lwp *lp, struct proc *);
static void bsd4_uload_update(struct lwp *lp);
static void bsd4_yield(struct lwp *lp);
static void bsd4_need_user_resched_remote(void *dummy);
static int bsd4_batchy_looser_pri_test(struct lwp* lp);
static struct lwp *bsd4_chooseproc_locked_cache_coherent(struct lwp *chklp);
static void bsd4_kick_helper(struct lwp *lp);
static struct lwp *bsd4_chooseproc_locked(struct lwp *chklp);
static void bsd4_remrunqueue_locked(struct lwp *lp);
static void bsd4_setrunqueue_locked(struct lwp *lp);
static void bsd4_changedcpu(struct lwp *lp);

struct usched usched_bsd4 = {
	{ NULL },
	"bsd4", "Original DragonFly Scheduler",
	NULL,			/* default registration */
	NULL,			/* default deregistration */
	bsd4_acquire_curproc,
	bsd4_release_curproc,
	bsd4_setrunqueue,
	bsd4_schedulerclock,
	bsd4_recalculate_estcpu,
	bsd4_resetpriority,
	bsd4_forking,
	bsd4_exiting,
	bsd4_uload_update,
	NULL,			/* setcpumask not supported */
	bsd4_yield,
	bsd4_changedcpu
};

struct usched_bsd4_pcpu {
	struct thread	helper_thread;
	short		rrcount;
	short		upri;
	struct lwp	*uschedcp;
	struct lwp	*old_uschedcp;
	cpu_node_t	*cpunode;
};

typedef struct usched_bsd4_pcpu	*bsd4_pcpu_t;

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
static struct rq bsd4_queues[NQS];
static struct rq bsd4_rtqueues[NQS];
static struct rq bsd4_idqueues[NQS];
static u_int32_t bsd4_queuebits;
static u_int32_t bsd4_rtqueuebits;
static u_int32_t bsd4_idqueuebits;
/* currently running a user process */
static cpumask_t bsd4_curprocmask = CPUMASK_INITIALIZER_ALLONES;
/* ready to accept a user process */
static cpumask_t bsd4_rdyprocmask;
static int	 bsd4_runqcount;
static volatile int bsd4_scancpu;
static struct spinlock bsd4_spin;
static struct usched_bsd4_pcpu bsd4_pcpu[MAXCPU];
static struct sysctl_ctx_list usched_bsd4_sysctl_ctx;
static struct sysctl_oid *usched_bsd4_sysctl_tree;

/* Debug info exposed through debug.* sysctl */

SYSCTL_INT(_debug, OID_AUTO, bsd4_runqcount, CTLFLAG_RD,
	   &bsd4_runqcount, 0,
	   "Number of run queues");

static int usched_bsd4_debug = -1;
SYSCTL_INT(_debug, OID_AUTO, bsd4_scdebug, CTLFLAG_RW,
	   &usched_bsd4_debug, 0,
	   "Print debug information for this pid");

static int usched_bsd4_pid_debug = -1;
SYSCTL_INT(_debug, OID_AUTO, bsd4_pid_debug, CTLFLAG_RW,
	   &usched_bsd4_pid_debug, 0,
	   "Print KTR debug information for this pid");

/* Tunning usched_bsd4 - configurable through kern.usched_bsd4.* */
static int usched_bsd4_smt = 0;
static int usched_bsd4_cache_coherent = 0;
static int usched_bsd4_upri_affinity = 16; /* 32 queues - half-way */
static int usched_bsd4_queue_checks = 5;
static int usched_bsd4_stick_to_level = 0;
static long usched_bsd4_kicks;
static int usched_bsd4_rrinterval = (ESTCPUFREQ + 9) / 10;
static int usched_bsd4_decay = 8;
static int usched_bsd4_batch_time = 10;

/* KTR debug printings */

KTR_INFO_MASTER_EXTERN(usched);

#if !defined(KTR_USCHED_BSD4)
#define	KTR_USCHED_BSD4	KTR_ALL
#endif

KTR_INFO(KTR_USCHED_BSD4, usched, bsd4_acquire_curproc_urw, 0,
    "USCHED_BSD4(bsd4_acquire_curproc in user_reseched_wanted "
    "after release: pid %d, cpuid %d, curr_cpuid %d)",
    pid_t pid, int cpuid, int curr);
KTR_INFO(KTR_USCHED_BSD4, usched, bsd4_acquire_curproc_before_loop, 0,
    "USCHED_BSD4(bsd4_acquire_curproc before loop: pid %d, cpuid %d, "
    "curr_cpuid %d)",
    pid_t pid, int cpuid, int curr);
KTR_INFO(KTR_USCHED_BSD4, usched, bsd4_acquire_curproc_not, 0,
    "USCHED_BSD4(bsd4_acquire_curproc couldn't acquire after "
    "bsd4_setrunqueue: pid %d, cpuid %d, curr_lp pid %d, curr_cpuid %d)",
    pid_t pid, int cpuid, pid_t curr_pid, int curr_cpuid);
KTR_INFO(KTR_USCHED_BSD4, usched, bsd4_acquire_curproc_switch, 0,
    "USCHED_BSD4(bsd4_acquire_curproc after lwkt_switch: pid %d, "
    "cpuid %d, curr_cpuid %d)",
    pid_t pid, int cpuid, int curr);

KTR_INFO(KTR_USCHED_BSD4, usched, bsd4_release_curproc, 0,
    "USCHED_BSD4(bsd4_release_curproc before select: pid %d, "
    "cpuid %d, curr_cpuid %d)",
    pid_t pid, int cpuid, int curr);

KTR_INFO(KTR_USCHED_BSD4, usched, bsd4_select_curproc, 0,
    "USCHED_BSD4(bsd4_release_curproc before select: pid %d, "
    "cpuid %d, old_pid %d, old_cpuid %d, curr_cpuid %d)",
    pid_t pid, int cpuid, pid_t old_pid, int old_cpuid, int curr);

KTR_INFO(KTR_USCHED_BSD4, usched, batchy_test_false, 0,
    "USCHED_BSD4(batchy_looser_pri_test false: pid %d, "
    "cpuid %d, verify_mask %lu)",
    pid_t pid, int cpuid, unsigned long mask);
KTR_INFO(KTR_USCHED_BSD4, usched, batchy_test_true, 0,
    "USCHED_BSD4(batchy_looser_pri_test true: pid %d, "
    "cpuid %d, verify_mask %lu)",
    pid_t pid, int cpuid, unsigned long mask);

KTR_INFO(KTR_USCHED_BSD4, usched, bsd4_setrunqueue_fc_smt, 0,
    "USCHED_BSD4(bsd4_setrunqueue free cpus smt: pid %d, cpuid %d, "
    "mask %lu, curr_cpuid %d)",
    pid_t pid, int cpuid, unsigned long mask, int curr);
KTR_INFO(KTR_USCHED_BSD4, usched, bsd4_setrunqueue_fc_non_smt, 0,
    "USCHED_BSD4(bsd4_setrunqueue free cpus check non_smt: pid %d, "
    "cpuid %d, mask %lu, curr_cpuid %d)",
    pid_t pid, int cpuid, unsigned long mask, int curr);
KTR_INFO(KTR_USCHED_BSD4, usched, bsd4_setrunqueue_rc, 0,
    "USCHED_BSD4(bsd4_setrunqueue running cpus check: pid %d, "
    "cpuid %d, mask %lu, curr_cpuid %d)",
    pid_t pid, int cpuid, unsigned long mask, int curr);
KTR_INFO(KTR_USCHED_BSD4, usched, bsd4_setrunqueue_found, 0,
    "USCHED_BSD4(bsd4_setrunqueue found cpu: pid %d, cpuid %d, "
    "mask %lu, found_cpuid %d, curr_cpuid %d)",
    pid_t pid, int cpuid, unsigned long mask, int found_cpuid, int curr);
KTR_INFO(KTR_USCHED_BSD4, usched, bsd4_setrunqueue_not_found, 0,
    "USCHED_BSD4(bsd4_setrunqueue not found cpu: pid %d, cpuid %d, "
    "try_cpuid %d, curr_cpuid %d)",
    pid_t pid, int cpuid, int try_cpuid, int curr);
KTR_INFO(KTR_USCHED_BSD4, usched, bsd4_setrunqueue_found_best_cpuid, 0,
    "USCHED_BSD4(bsd4_setrunqueue found cpu: pid %d, cpuid %d, "
    "mask %lu, found_cpuid %d, curr_cpuid %d)",
    pid_t pid, int cpuid, unsigned long mask, int found_cpuid, int curr);

KTR_INFO(KTR_USCHED_BSD4, usched, chooseproc, 0,
    "USCHED_BSD4(chooseproc: pid %d, old_cpuid %d, curr_cpuid %d)",
    pid_t pid, int old_cpuid, int curr);
KTR_INFO(KTR_USCHED_BSD4, usched, chooseproc_cc, 0,
    "USCHED_BSD4(chooseproc_cc: pid %d, old_cpuid %d, curr_cpuid %d)",
    pid_t pid, int old_cpuid, int curr);
KTR_INFO(KTR_USCHED_BSD4, usched, chooseproc_cc_not_good, 0,
    "USCHED_BSD4(chooseproc_cc not good: pid %d, old_cpumask %lu, "
    "sibling_mask %lu, curr_cpumask %lu)",
    pid_t pid, unsigned long old_cpumask, unsigned long sibling_mask, unsigned long curr);
KTR_INFO(KTR_USCHED_BSD4, usched, chooseproc_cc_elected, 0,
    "USCHED_BSD4(chooseproc_cc elected: pid %d, old_cpumask %lu, "
    "sibling_mask %lu, curr_cpumask: %lu)",
    pid_t pid, unsigned long old_cpumask, unsigned long sibling_mask, unsigned long curr);

KTR_INFO(KTR_USCHED_BSD4, usched, sched_thread_no_process, 0,
    "USCHED_BSD4(sched_thread %d no process scheduled: pid %d, old_cpuid %d)",
    int id, pid_t pid, int cpuid);
KTR_INFO(KTR_USCHED_BSD4, usched, sched_thread_process, 0,
    "USCHED_BSD4(sched_thread %d process scheduled: pid %d, old_cpuid %d)",
    int id, pid_t pid, int cpuid);
KTR_INFO(KTR_USCHED_BSD4, usched, sched_thread_no_process_found, 0,
    "USCHED_BSD4(sched_thread %d no process found; tmpmask %lu)",
    int id, unsigned long tmpmask);

/*
 * Initialize the run queues at boot time.
 */
static void
bsd4_rqinit(void *dummy)
{
	int i;

	spin_init(&bsd4_spin, "bsd4rq");
	for (i = 0; i < NQS; i++) {
		TAILQ_INIT(&bsd4_queues[i]);
		TAILQ_INIT(&bsd4_rtqueues[i]);
		TAILQ_INIT(&bsd4_idqueues[i]);
	}
	ATOMIC_CPUMASK_NANDBIT(bsd4_curprocmask, 0);
}
SYSINIT(runqueue, SI_BOOT2_USCHED, SI_ORDER_FIRST, bsd4_rqinit, NULL)

/*
 * BSD4_ACQUIRE_CURPROC
 *
 * This function is called when the kernel intends to return to userland.
 * It is responsible for making the thread the current designated userland
 * thread for this cpu, blocking if necessary.
 *
 * The kernel will not depress our LWKT priority until after we return,
 * in case we have to shove over to another cpu.
 *
 * We must determine our thread's disposition before we switch away.  This
 * is very sensitive code.
 *
 * WARNING! THIS FUNCTION IS ALLOWED TO CAUSE THE CURRENT THREAD TO MIGRATE
 * TO ANOTHER CPU!  Because most of the kernel assumes that no migration will
 * occur, this function is called only under very controlled circumstances.
 *
 * MPSAFE
 */
static void
bsd4_acquire_curproc(struct lwp *lp)
{
	globaldata_t gd;
	bsd4_pcpu_t dd;
	thread_t td;
#if 0
	struct lwp *olp;
#endif

	/*
	 * Make sure we aren't sitting on a tsleep queue.
	 */
	td = lp->lwp_thread;
	crit_enter_quick(td);
	if (td->td_flags & TDF_TSLEEPQ)
		tsleep_remove(td);
	bsd4_recalculate_estcpu(lp);

	/*
	 * If a reschedule was requested give another thread the
	 * driver's seat.
	 */
	if (user_resched_wanted()) {
		clear_user_resched();
		bsd4_release_curproc(lp);

		KTR_COND_LOG(usched_bsd4_acquire_curproc_urw,
		    lp->lwp_proc->p_pid == usched_bsd4_pid_debug,
		    lp->lwp_proc->p_pid,
		    lp->lwp_thread->td_gd->gd_cpuid,
		    mycpu->gd_cpuid);
	}

	/*
	 * Loop until we are the current user thread
	 */
	gd = mycpu;
	dd = &bsd4_pcpu[gd->gd_cpuid];

	KTR_COND_LOG(usched_bsd4_acquire_curproc_before_loop,
	    lp->lwp_proc->p_pid == usched_bsd4_pid_debug,
	    lp->lwp_proc->p_pid,
	    lp->lwp_thread->td_gd->gd_cpuid,
	    gd->gd_cpuid);

	do {
		/*
		 * Process any pending events and higher priority threads.
		 */
		lwkt_yield();

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
		} else if (dd->uschedcp == NULL) {
			/*
			 * We can trivially become the current lwp.
			 */
			ATOMIC_CPUMASK_ORBIT(bsd4_curprocmask, gd->gd_cpuid);
			dd->uschedcp = lp;
			dd->upri = lp->lwp_priority;
		} else if (dd->upri > lp->lwp_priority) {
			/*
			 * We can steal the current cpu's lwp designation
			 * away simply by replacing it.  The other thread
			 * will stall when it tries to return to userland.
			 */
			dd->uschedcp = lp;
			dd->upri = lp->lwp_priority;
			/*
			lwkt_deschedule(olp->lwp_thread);
			bsd4_setrunqueue(olp);
			*/
		} else {
			/*
			 * We cannot become the current lwp, place the lp
			 * on the bsd4 run-queue and deschedule ourselves.
			 *
			 * When we are reactivated we will have another
			 * chance.
			 */
			lwkt_deschedule(lp->lwp_thread);

			bsd4_setrunqueue(lp);

			KTR_COND_LOG(usched_bsd4_acquire_curproc_not,
			    lp->lwp_proc->p_pid == usched_bsd4_pid_debug,
			    lp->lwp_proc->p_pid,
			    lp->lwp_thread->td_gd->gd_cpuid,
			    dd->uschedcp->lwp_proc->p_pid,
			    gd->gd_cpuid);


			lwkt_switch();

			/*
			 * Reload after a switch or setrunqueue/switch possibly
			 * moved us to another cpu.
			 */
			gd = mycpu;
			dd = &bsd4_pcpu[gd->gd_cpuid];

			KTR_COND_LOG(usched_bsd4_acquire_curproc_switch,
			    lp->lwp_proc->p_pid == usched_bsd4_pid_debug,
			    lp->lwp_proc->p_pid,
			    lp->lwp_thread->td_gd->gd_cpuid,
			    gd->gd_cpuid);
		}
	} while (dd->uschedcp != lp);

	crit_exit_quick(td);
	KKASSERT((lp->lwp_mpflags & LWP_MP_ONRUNQ) == 0);
}

/*
 * BSD4_RELEASE_CURPROC
 *
 * This routine detaches the current thread from the userland scheduler,
 * usually because the thread needs to run or block in the kernel (at
 * kernel priority) for a while.
 *
 * This routine is also responsible for selecting a new thread to
 * make the current thread.
 *
 * NOTE: This implementation differs from the dummy example in that
 * bsd4_select_curproc() is able to select the current process, whereas
 * dummy_select_curproc() is not able to select the current process.
 * This means we have to NULL out uschedcp.
 *
 * Additionally, note that we may already be on a run queue if releasing
 * via the lwkt_switch() in bsd4_setrunqueue().
 *
 * MPSAFE
 */

static void
bsd4_release_curproc(struct lwp *lp)
{
	globaldata_t gd = mycpu;
	bsd4_pcpu_t dd = &bsd4_pcpu[gd->gd_cpuid];

	if (dd->uschedcp == lp) {
		crit_enter();
		KKASSERT((lp->lwp_mpflags & LWP_MP_ONRUNQ) == 0);

		KTR_COND_LOG(usched_bsd4_release_curproc,
		    lp->lwp_proc->p_pid == usched_bsd4_pid_debug,
		    lp->lwp_proc->p_pid,
		    lp->lwp_thread->td_gd->gd_cpuid,
		    gd->gd_cpuid);

		dd->uschedcp = NULL;	/* don't let lp be selected */
		dd->upri = PRIBASE_NULL;
		ATOMIC_CPUMASK_NANDBIT(bsd4_curprocmask, gd->gd_cpuid);
		dd->old_uschedcp = lp;	/* used only for KTR debug prints */
		bsd4_select_curproc(gd);
		crit_exit();
	}
}

/*
 * BSD4_SELECT_CURPROC
 *
 * Select a new current process for this cpu and clear any pending user
 * reschedule request.  The cpu currently has no current process.
 *
 * This routine is also responsible for equal-priority round-robining,
 * typically triggered from bsd4_schedulerclock().  In our dummy example
 * all the 'user' threads are LWKT scheduled all at once and we just
 * call lwkt_switch().
 *
 * The calling process is not on the queue and cannot be selected.
 *
 * MPSAFE
 */
static
void
bsd4_select_curproc(globaldata_t gd)
{
	bsd4_pcpu_t dd = &bsd4_pcpu[gd->gd_cpuid];
	struct lwp *nlp;
	int cpuid = gd->gd_cpuid;

	crit_enter_gd(gd);

	spin_lock(&bsd4_spin);
	if(usched_bsd4_cache_coherent)
		nlp = bsd4_chooseproc_locked_cache_coherent(dd->uschedcp);
	else
		nlp = bsd4_chooseproc_locked(dd->uschedcp);

	if (nlp) {

		KTR_COND_LOG(usched_bsd4_select_curproc,
		    nlp->lwp_proc->p_pid == usched_bsd4_pid_debug,
		    nlp->lwp_proc->p_pid,
		    nlp->lwp_thread->td_gd->gd_cpuid,
		    dd->old_uschedcp->lwp_proc->p_pid,
		    dd->old_uschedcp->lwp_thread->td_gd->gd_cpuid,
		    gd->gd_cpuid);

		ATOMIC_CPUMASK_ORBIT(bsd4_curprocmask, cpuid);
		dd->upri = nlp->lwp_priority;
		dd->uschedcp = nlp;
		dd->rrcount = 0;		/* reset round robin */
		spin_unlock(&bsd4_spin);
		lwkt_acquire(nlp->lwp_thread);
		lwkt_schedule(nlp->lwp_thread);
	} else {
		spin_unlock(&bsd4_spin);
	}

#if 0
	} else if (bsd4_runqcount && CPUMASK_TESTBIT(bsd4_rdyprocmask, cpuid)) {
		ATOMIC_CPUMASK_NANDBIT(bsd4_rdyprocmask, cpuid);
		spin_unlock(&bsd4_spin);
		lwkt_schedule(&dd->helper_thread);
	} else {
		spin_unlock(&bsd4_spin);
	}
#endif
	crit_exit_gd(gd);
}

/*
 * batchy_looser_pri_test() - determine if a process is batchy or not
 * relative to the other processes running in the system
 */
static int
bsd4_batchy_looser_pri_test(struct lwp* lp)
{
	cpumask_t mask;
	bsd4_pcpu_t other_dd;
	int cpu;

	/* Current running processes */
	mask = bsd4_curprocmask;
	CPUMASK_ANDMASK(mask, smp_active_mask);
	CPUMASK_ANDMASK(mask, usched_global_cpumask);

	while (CPUMASK_TESTNZERO(mask)) {
		cpu = BSFCPUMASK(mask);
		other_dd = &bsd4_pcpu[cpu];
		if (other_dd->upri - lp->lwp_priority > usched_bsd4_upri_affinity * PPQ) {

			KTR_COND_LOG(usched_batchy_test_false,
			    lp->lwp_proc->p_pid == usched_bsd4_pid_debug,
			    lp->lwp_proc->p_pid,
			    lp->lwp_thread->td_gd->gd_cpuid,
			    (unsigned long)CPUMASK_LOWMASK(mask));

			return 0;
		}
		CPUMASK_NANDBIT(mask, cpu);
	}

	KTR_COND_LOG(usched_batchy_test_true,
	    lp->lwp_proc->p_pid == usched_bsd4_pid_debug,
	    lp->lwp_proc->p_pid,
	    lp->lwp_thread->td_gd->gd_cpuid,
	    (unsigned long)CPUMASK_LOWMASK(mask));

	return 1;
}

/*
 *
 * BSD4_SETRUNQUEUE
 *
 * Place the specified lwp on the user scheduler's run queue.  This routine
 * must be called with the thread descheduled.  The lwp must be runnable.
 *
 * The thread may be the current thread as a special case.
 *
 * MPSAFE
 */
static void
bsd4_setrunqueue(struct lwp *lp)
{
	globaldata_t gd;
	bsd4_pcpu_t dd;
	int cpuid;
	cpumask_t mask;
	cpumask_t tmpmask;

	/*
	 * First validate the process state relative to the current cpu.
	 * We don't need the spinlock for this, just a critical section.
	 * We are in control of the process.
	 */
	crit_enter();
	KASSERT(lp->lwp_stat == LSRUN, ("setrunqueue: lwp not LSRUN"));
	KASSERT((lp->lwp_mpflags & LWP_MP_ONRUNQ) == 0,
	    ("lwp %d/%d already on runq! flag %08x/%08x", lp->lwp_proc->p_pid,
	     lp->lwp_tid, lp->lwp_proc->p_flags, lp->lwp_flags));
	KKASSERT((lp->lwp_thread->td_flags & TDF_RUNQ) == 0);

	/*
	 * Note: gd and dd are relative to the target thread's last cpu,
	 * NOT our current cpu.
	 */
	gd = lp->lwp_thread->td_gd;
	dd = &bsd4_pcpu[gd->gd_cpuid];

	/*
	 * This process is not supposed to be scheduled anywhere or assigned
	 * as the current process anywhere.  Assert the condition.
	 */
	KKASSERT(dd->uschedcp != lp);

	/*
	 * XXX fixme.  Could be part of a remrunqueue/setrunqueue
	 * operation when the priority is recalculated, so TDF_MIGRATING
	 * may already be set.
	 */
	if ((lp->lwp_thread->td_flags & TDF_MIGRATING) == 0)
		lwkt_giveaway(lp->lwp_thread);

	/*
	 * We lose control of lp the moment we release the spinlock after
	 * having placed lp on the queue.  i.e. another cpu could pick it
	 * up and it could exit, or its priority could be further adjusted,
	 * or something like that.
	 */
	spin_lock(&bsd4_spin);
	bsd4_setrunqueue_locked(lp);
	lp->lwp_rebal_ticks = sched_ticks;

	/*
	 * Kick the scheduler helper on one of the other cpu's
	 * and request a reschedule if appropriate.
	 *
	 * NOTE: We check all cpus whos rdyprocmask is set.  First we
	 *	 look for cpus without designated lps, then we look for
	 *	 cpus with designated lps with a worse priority than our
	 *	 process.
	 */
	++bsd4_scancpu;

	if (usched_bsd4_smt) {

		/*
		 * SMT heuristic - Try to schedule on a free physical core.
		 * If no physical core found than choose the one that has
		 * an interactive thread.
		 */

		int best_cpuid = -1;
		int min_prio = MAXPRI * MAXPRI;
		int sibling;

		cpuid = (bsd4_scancpu & 0xFFFF) % ncpus;
		mask = bsd4_rdyprocmask;
		CPUMASK_NANDMASK(mask, bsd4_curprocmask);
		CPUMASK_ANDMASK(mask, lp->lwp_cpumask);
		CPUMASK_ANDMASK(mask, smp_active_mask);
		CPUMASK_ANDMASK(mask, usched_global_cpumask);

		KTR_COND_LOG(usched_bsd4_setrunqueue_fc_smt,
		    lp->lwp_proc->p_pid == usched_bsd4_pid_debug,
		    lp->lwp_proc->p_pid,
		    lp->lwp_thread->td_gd->gd_cpuid,
		    (unsigned long)CPUMASK_LOWMASK(mask),
		    mycpu->gd_cpuid);

		while (CPUMASK_TESTNZERO(mask)) {
			CPUMASK_ASSNBMASK(tmpmask, cpuid);
			if (CPUMASK_TESTMASK(tmpmask, mask)) {
				CPUMASK_ANDMASK(tmpmask, mask);
				cpuid = BSFCPUMASK(tmpmask);
			} else {
				cpuid = BSFCPUMASK(mask);
			}
			gd = globaldata_find(cpuid);
			dd = &bsd4_pcpu[cpuid];

			if ((dd->upri & ~PPQMASK) >= (lp->lwp_priority & ~PPQMASK)) {
				tmpmask = dd->cpunode->parent_node->members;
				CPUMASK_NANDMASK(tmpmask, dd->cpunode->members);
				CPUMASK_ANDMASK(tmpmask, mask);
				if (CPUMASK_TESTNZERO(tmpmask)) {
					KTR_COND_LOG(usched_bsd4_setrunqueue_found,
					    lp->lwp_proc->p_pid == usched_bsd4_pid_debug,
					    lp->lwp_proc->p_pid,
					    lp->lwp_thread->td_gd->gd_cpuid,
					    (unsigned long)CPUMASK_LOWMASK(mask),
					    cpuid,
					    mycpu->gd_cpuid);

					goto found;
				} else {
					tmpmask =
					    dd->cpunode->parent_node->members;
					CPUMASK_NANDMASK(tmpmask,
					    dd->cpunode->members);
					sibling = BSFCPUMASK(tmpmask);
					if (min_prio >
					    bsd4_pcpu[sibling].upri) {
						min_prio =
							bsd4_pcpu[sibling].upri;
						best_cpuid = cpuid;
					}
				}
			}
			CPUMASK_NANDBIT(mask, cpuid);
		}

		if (best_cpuid != -1) {
			cpuid = best_cpuid;
			gd = globaldata_find(cpuid);
			dd = &bsd4_pcpu[cpuid];

			KTR_COND_LOG(usched_bsd4_setrunqueue_found_best_cpuid,
			    lp->lwp_proc->p_pid == usched_bsd4_pid_debug,
			    lp->lwp_proc->p_pid,
			    lp->lwp_thread->td_gd->gd_cpuid,
			    (unsigned long)CPUMASK_LOWMASK(mask),
			    cpuid,
			    mycpu->gd_cpuid);

			goto found;
		}
	} else {
		/* Fallback to the original heuristic */
		cpuid = (bsd4_scancpu & 0xFFFF) % ncpus;
		mask = bsd4_rdyprocmask;
		CPUMASK_NANDMASK(mask, bsd4_curprocmask);
		CPUMASK_ANDMASK(mask, lp->lwp_cpumask);
		CPUMASK_ANDMASK(mask, smp_active_mask);
		CPUMASK_ANDMASK(mask, usched_global_cpumask);

		KTR_COND_LOG(usched_bsd4_setrunqueue_fc_non_smt,
		    lp->lwp_proc->p_pid == usched_bsd4_pid_debug,
		    lp->lwp_proc->p_pid,
		    lp->lwp_thread->td_gd->gd_cpuid,
		    (unsigned long)CPUMASK_LOWMASK(mask),
		    mycpu->gd_cpuid);

		while (CPUMASK_TESTNZERO(mask)) {
			CPUMASK_ASSNBMASK(tmpmask, cpuid);
			if (CPUMASK_TESTMASK(tmpmask, mask)) {
				CPUMASK_ANDMASK(tmpmask, mask);
				cpuid = BSFCPUMASK(tmpmask);
			} else {
				cpuid = BSFCPUMASK(mask);
			}
			gd = globaldata_find(cpuid);
			dd = &bsd4_pcpu[cpuid];

			if ((dd->upri & ~PPQMASK) >=
			    (lp->lwp_priority & ~PPQMASK)) {
				KTR_COND_LOG(usched_bsd4_setrunqueue_found,
				    lp->lwp_proc->p_pid == usched_bsd4_pid_debug,
				    lp->lwp_proc->p_pid,
				    lp->lwp_thread->td_gd->gd_cpuid,
				    (unsigned long)CPUMASK_LOWMASK(mask),
				    cpuid,
				    mycpu->gd_cpuid);

				goto found;
			}
			CPUMASK_NANDBIT(mask, cpuid);
		}
	}

	/*
	 * Then cpus which might have a currently running lp
	 */
	mask = bsd4_curprocmask;
	CPUMASK_ANDMASK(mask, bsd4_rdyprocmask);
	CPUMASK_ANDMASK(mask, lp->lwp_cpumask);
	CPUMASK_ANDMASK(mask, smp_active_mask);
	CPUMASK_ANDMASK(mask, usched_global_cpumask);

	KTR_COND_LOG(usched_bsd4_setrunqueue_rc,
	    lp->lwp_proc->p_pid == usched_bsd4_pid_debug,
	    lp->lwp_proc->p_pid,
	    lp->lwp_thread->td_gd->gd_cpuid,
	    (unsigned long)CPUMASK_LOWMASK(mask),
	    mycpu->gd_cpuid);

	while (CPUMASK_TESTNZERO(mask)) {
		CPUMASK_ASSNBMASK(tmpmask, cpuid);
		if (CPUMASK_TESTMASK(tmpmask, mask)) {
			CPUMASK_ANDMASK(tmpmask, mask);
			cpuid = BSFCPUMASK(tmpmask);
		} else {
			cpuid = BSFCPUMASK(mask);
		}
		gd = globaldata_find(cpuid);
		dd = &bsd4_pcpu[cpuid];

		if ((dd->upri & ~PPQMASK) > (lp->lwp_priority & ~PPQMASK)) {
			KTR_COND_LOG(usched_bsd4_setrunqueue_found,
			    lp->lwp_proc->p_pid == usched_bsd4_pid_debug,
			    lp->lwp_proc->p_pid,
			    lp->lwp_thread->td_gd->gd_cpuid,
			    (unsigned long)CPUMASK_LOWMASK(mask),
			    cpuid,
			    mycpu->gd_cpuid);

			goto found;
		}
		CPUMASK_NANDBIT(mask, cpuid);
	}

	/*
	 * If we cannot find a suitable cpu we reload from bsd4_scancpu
	 * and round-robin.  Other cpus will pickup as they release their
	 * current lwps or become ready.
	 *
	 * Avoid a degenerate system lockup case if usched_global_cpumask
	 * is set to 0 or otherwise does not cover lwp_cpumask.
	 *
	 * We only kick the target helper thread in this case, we do not
	 * set the user resched flag because
	 */
	cpuid = (bsd4_scancpu & 0xFFFF) % ncpus;
	if (CPUMASK_TESTBIT(usched_global_cpumask, cpuid) == 0)
		cpuid = 0;
	gd = globaldata_find(cpuid);
	dd = &bsd4_pcpu[cpuid];

	KTR_COND_LOG(usched_bsd4_setrunqueue_not_found,
	    lp->lwp_proc->p_pid == usched_bsd4_pid_debug,
	    lp->lwp_proc->p_pid,
	    lp->lwp_thread->td_gd->gd_cpuid,
	    cpuid,
	    mycpu->gd_cpuid);

found:
	if (gd == mycpu) {
		spin_unlock(&bsd4_spin);
		if ((dd->upri & ~PPQMASK) > (lp->lwp_priority & ~PPQMASK)) {
			if (dd->uschedcp == NULL) {
				wakeup_mycpu(&dd->helper_thread);
			} else {
				need_user_resched();
			}
		}
	} else {
		ATOMIC_CPUMASK_NANDBIT(bsd4_rdyprocmask, cpuid);
		spin_unlock(&bsd4_spin);
		if ((dd->upri & ~PPQMASK) > (lp->lwp_priority & ~PPQMASK))
			lwkt_send_ipiq(gd, bsd4_need_user_resched_remote, NULL);
		else
			wakeup(&dd->helper_thread);
	}
	crit_exit();
}

/*
 * This routine is called from a systimer IPI.  It MUST be MP-safe and
 * the BGL IS NOT HELD ON ENTRY.  This routine is called at ESTCPUFREQ on
 * each cpu.
 *
 * This routine is called on every sched tick.  If the currently running
 * thread belongs to this scheduler it will be called with a non-NULL lp,
 * otherwise it will be called with a NULL lp.
 *
 * MPSAFE
 */
static
void
bsd4_schedulerclock(struct lwp *lp, sysclock_t period, sysclock_t cpstamp)
{
	globaldata_t gd = mycpu;
	bsd4_pcpu_t dd = &bsd4_pcpu[gd->gd_cpuid];

	/*
	 * No impl if no lp running.
	 */
	if (lp == NULL)
		return;

	/*
	 * Do we need to round-robin?  We round-robin 10 times a second.
	 * This should only occur for cpu-bound batch processes.
	 */
	if (++dd->rrcount >= usched_bsd4_rrinterval) {
		dd->rrcount = 0;
		need_user_resched();
	}

	/*
	 * Adjust estcpu upward using a real time equivalent calculation.
	 */
	lp->lwp_estcpu = ESTCPULIM(lp->lwp_estcpu + ESTCPUMAX / ESTCPUFREQ + 1);

	/*
	 * Spinlocks also hold a critical section so there should not be
	 * any active.
	 */
	KKASSERT(gd->gd_spinlocks == 0);

	bsd4_resetpriority(lp);
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
 *
 * MPSAFE
 */
static
void
bsd4_recalculate_estcpu(struct lwp *lp)
{
	globaldata_t gd = mycpu;
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
		bsd4_resetpriority(lp);
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
		if ((ssysclock_t)ttlticks < 0) {
			ttlticks = 0;
			lp->lwp_cpbase = cpbase;
		}
		if (ttlticks == 0)
			return;
		updatepcpu(lp, lp->lwp_cpticks, ttlticks);

		/*
		 * Calculate the percentage of one cpu used factoring in ncpus
		 * and the load and adjust estcpu.  Handle degenerate cases
		 * by adding 1 to bsd4_runqcount.
		 *
		 * estcpu is scaled by ESTCPUMAX.
		 *
		 * bsd4_runqcount is the excess number of user processes
		 * that cannot be immediately scheduled to cpus.  We want
		 * to count these as running to avoid range compression
		 * in the base calculation (which is the actual percentage
		 * of one cpu used).
		 */
		estcpu = (lp->lwp_cpticks * ESTCPUMAX) *
			 (bsd4_runqcount + ncpus) / (ncpus * ttlticks);

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

		if (usched_bsd4_debug == lp->lwp_proc->p_pid) {
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
		 * NOTE: p_nice is accounted for in bsd4_resetpriority(),
		 *	 and not here, but we must still ensure that a
		 *	 cpu-bound nice -20 process does not completely
		 *	 override a cpu-bound nice +20 process.
		 *
		 * NOTE: We must use ESTCPULIM() here to deal with any
		 *	 overshoot.
		 */
		decay_factor = usched_bsd4_decay;
		if (decay_factor < 1)
			decay_factor = 1;
		if (decay_factor > 1024)
			decay_factor = 1024;

		lp->lwp_estcpu = ESTCPULIM(
			(lp->lwp_estcpu * decay_factor + estcpu) /
			(decay_factor + 1));

		if (usched_bsd4_debug == lp->lwp_proc->p_pid)
			kprintf(" finalestcpu %d\n", lp->lwp_estcpu);
		bsd4_resetpriority(lp);
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
 *
 * MPSAFE
 */
static void
bsd4_resetpriority(struct lwp *lp)
{
	bsd4_pcpu_t dd;
	int newpriority;
	u_short newrqtype;
	int reschedcpu;
	int checkpri;
	int estcpu;

	/*
	 * Calculate the new priority and queue type
	 */
	crit_enter();
	spin_lock(&bsd4_spin);

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
	 * td_upri has normal sense (higher values are more desireable), so
	 * negate it.
	 */
	lp->lwp_thread->td_upri = -(newpriority & ~PPQMASK);
	if ((lp->lwp_priority ^ newpriority) & ~PPQMASK) {
		lp->lwp_priority = newpriority;
		if (lp->lwp_mpflags & LWP_MP_ONRUNQ) {
			bsd4_remrunqueue_locked(lp);
			lp->lwp_rqtype = newrqtype;
			lp->lwp_rqindex = (newpriority & PRIMASK) / PPQ;
			bsd4_setrunqueue_locked(lp);
			checkpri = 1;
		} else {
			lp->lwp_rqtype = newrqtype;
			lp->lwp_rqindex = (newpriority & PRIMASK) / PPQ;
			checkpri = 0;
		}
		reschedcpu = lp->lwp_thread->td_gd->gd_cpuid;
	} else {
		lp->lwp_priority = newpriority;
		reschedcpu = -1;
		checkpri = 1;
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
	if (reschedcpu >= 0) {
		dd = &bsd4_pcpu[reschedcpu];
		if (CPUMASK_TESTBIT(bsd4_rdyprocmask, reschedcpu) &&
		    (checkpri == 0 ||
		     (dd->upri & ~PRIMASK) > (lp->lwp_priority & ~PRIMASK))) {
			if (reschedcpu == mycpu->gd_cpuid) {
				spin_unlock(&bsd4_spin);
				need_user_resched();
			} else {
				spin_unlock(&bsd4_spin);
				ATOMIC_CPUMASK_NANDBIT(bsd4_rdyprocmask,
						       reschedcpu);
				lwkt_send_ipiq(lp->lwp_thread->td_gd,
					       bsd4_need_user_resched_remote,
					       NULL);
			}
		} else {
			spin_unlock(&bsd4_spin);
		}
	} else {
		spin_unlock(&bsd4_spin);
	}
	crit_exit();
}

/*
 * MPSAFE
 */
static
void
bsd4_yield(struct lwp *lp)
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

static
void
bsd4_changedcpu(struct lwp *lp __unused)
{
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
 *
 * MPSAFE
 */
static void
bsd4_forking(struct lwp *plp, struct lwp *lp)
{
	/*
	 * Put the child 4 queue slots (out of 32) higher than the parent
	 * (less desireable than the parent).
	 */
	lp->lwp_estcpu = ESTCPULIM(plp->lwp_estcpu + ESTCPUPPQ * 4);

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
bsd4_exiting(struct lwp *lp, struct proc *child_proc)
{
}

static void
bsd4_uload_update(struct lwp *lp)
{
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
 * MPSAFE - must be called with bsd4_spin exclusive held.  The spinlock is
 *	    left intact through the entire routine.
 */
static
struct lwp *
bsd4_chooseproc_locked(struct lwp *chklp)
{
	struct lwp *lp;
	struct rq *q;
	u_int32_t *which, *which2;
	u_int32_t pri;
	u_int32_t rtqbits;
	u_int32_t tsqbits;
	u_int32_t idqbits;
	cpumask_t cpumask;

	rtqbits = bsd4_rtqueuebits;
	tsqbits = bsd4_queuebits;
	idqbits = bsd4_idqueuebits;
	cpumask = mycpu->gd_cpumask;


again:
	if (rtqbits) {
		pri = bsfl(rtqbits);
		q = &bsd4_rtqueues[pri];
		which = &bsd4_rtqueuebits;
		which2 = &rtqbits;
	} else if (tsqbits) {
		pri = bsfl(tsqbits);
		q = &bsd4_queues[pri];
		which = &bsd4_queuebits;
		which2 = &tsqbits;
	} else if (idqbits) {
		pri = bsfl(idqbits);
		q = &bsd4_idqueues[pri];
		which = &bsd4_idqueuebits;
		which2 = &idqbits;
	} else {
		return NULL;
	}
	lp = TAILQ_FIRST(q);
	KASSERT(lp, ("chooseproc: no lwp on busy queue"));

	while (CPUMASK_TESTMASK(lp->lwp_cpumask, cpumask) == 0) {
		lp = TAILQ_NEXT(lp, lwp_procq);
		if (lp == NULL) {
			*which2 &= ~(1 << pri);
			goto again;
		}
	}

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

	/*
	 * If the chosen lwp does not reside on this cpu spend a few
	 * cycles looking for a better candidate at the same priority level.
	 * This is a fallback check, setrunqueue() tries to wakeup the
	 * correct cpu and is our front-line affinity.
	 */
	if (lp->lwp_thread->td_gd != mycpu &&
	    (chklp = TAILQ_NEXT(lp, lwp_procq)) != NULL
	) {
		if (chklp->lwp_thread->td_gd == mycpu) {
			lp = chklp;
		}
	}

	KTR_COND_LOG(usched_chooseproc,
	    lp->lwp_proc->p_pid == usched_bsd4_pid_debug,
	    lp->lwp_proc->p_pid,
	    lp->lwp_thread->td_gd->gd_cpuid,
	    mycpu->gd_cpuid);

	TAILQ_REMOVE(q, lp, lwp_procq);
	--bsd4_runqcount;
	if (TAILQ_EMPTY(q))
		*which &= ~(1 << pri);
	KASSERT((lp->lwp_mpflags & LWP_MP_ONRUNQ) != 0, ("not on runq6!"));
	atomic_clear_int(&lp->lwp_mpflags, LWP_MP_ONRUNQ);

	return lp;
}

/*
 * chooseproc() - with a cache coherence heuristic. Try to pull a process that
 * has its home on the current CPU> If the process doesn't have its home here
 * and is a batchy one (see batcy_looser_pri_test), we can wait for a
 * sched_tick, may be its home will become free and pull it in. Anyway,
 * we can't wait more than one tick. If that tick expired, we pull in that
 * process, no matter what.
 */
static
struct lwp *
bsd4_chooseproc_locked_cache_coherent(struct lwp *chklp)
{
	struct lwp *lp;
	struct rq *q;
	u_int32_t *which, *which2;
	u_int32_t pri;
	u_int32_t checks;
	u_int32_t rtqbits;
	u_int32_t tsqbits;
	u_int32_t idqbits;
	cpumask_t cpumask;

	struct lwp * min_level_lwp = NULL;
	struct rq *min_q = NULL;
	cpumask_t siblings;
	cpu_node_t* cpunode = NULL;
	u_int32_t min_level = MAXCPU;	/* number of levels < MAXCPU */
	u_int32_t *min_which = NULL;
	u_int32_t min_pri = 0;
	u_int32_t level = 0;

	rtqbits = bsd4_rtqueuebits;
	tsqbits = bsd4_queuebits;
	idqbits = bsd4_idqueuebits;
	cpumask = mycpu->gd_cpumask;

	/* Get the mask coresponding to the sysctl configured level */
	cpunode = bsd4_pcpu[mycpu->gd_cpuid].cpunode;
	level = usched_bsd4_stick_to_level;
	while (level) {
		cpunode = cpunode->parent_node;
		level--;
	}
	/* The cpus which can ellect a process */
	siblings = cpunode->members;
	checks = 0;

again:
	if (rtqbits) {
		pri = bsfl(rtqbits);
		q = &bsd4_rtqueues[pri];
		which = &bsd4_rtqueuebits;
		which2 = &rtqbits;
	} else if (tsqbits) {
		pri = bsfl(tsqbits);
		q = &bsd4_queues[pri];
		which = &bsd4_queuebits;
		which2 = &tsqbits;
	} else if (idqbits) {
		pri = bsfl(idqbits);
		q = &bsd4_idqueues[pri];
		which = &bsd4_idqueuebits;
		which2 = &idqbits;
	} else {
		/*
		 * No more left and we didn't reach the checks limit.
		 */
		bsd4_kick_helper(min_level_lwp);
		return NULL;
	}
	lp = TAILQ_FIRST(q);
	KASSERT(lp, ("chooseproc: no lwp on busy queue"));

	/*
	 * Limit the number of checks/queue to a configurable value to
	 * minimize the contention (we are in a locked region
	 */
	while (checks < usched_bsd4_queue_checks) {
		if (CPUMASK_TESTMASK(lp->lwp_cpumask, cpumask) == 0 ||
		    (CPUMASK_TESTMASK(siblings,
				      lp->lwp_thread->td_gd->gd_cpumask) == 0 &&
		      (lp->lwp_rebal_ticks == sched_ticks ||
		       lp->lwp_rebal_ticks == (int)(sched_ticks - 1)) &&
		      bsd4_batchy_looser_pri_test(lp))) {

			KTR_COND_LOG(usched_chooseproc_cc_not_good,
			    lp->lwp_proc->p_pid == usched_bsd4_pid_debug,
			    lp->lwp_proc->p_pid,
			    (unsigned long)CPUMASK_LOWMASK(
					lp->lwp_thread->td_gd->gd_cpumask),
			    (unsigned long)CPUMASK_LOWMASK(siblings),
			    (unsigned long)CPUMASK_LOWMASK(cpumask));

			cpunode = bsd4_pcpu[lp->lwp_thread->td_gd->gd_cpuid].cpunode;
			level = 0;
			while (cpunode) {
				if (CPUMASK_TESTMASK(cpunode->members,
						     cpumask)) {
					break;
				}
				cpunode = cpunode->parent_node;
				level++;
			}
			if (level < min_level ||
			    (level == min_level && min_level_lwp &&
			     lp->lwp_priority < min_level_lwp->lwp_priority)) {
				bsd4_kick_helper(min_level_lwp);
				min_level_lwp = lp;
				min_level = level;
				min_q = q;
				min_which = which;
				min_pri = pri;
			} else {
				bsd4_kick_helper(lp);
			}
			lp = TAILQ_NEXT(lp, lwp_procq);
			if (lp == NULL) {
				*which2 &= ~(1 << pri);
				goto again;
			}
		} else {
			KTR_COND_LOG(usched_chooseproc_cc_elected,
			    lp->lwp_proc->p_pid == usched_bsd4_pid_debug,
			    lp->lwp_proc->p_pid,
			    (unsigned long)CPUMASK_LOWMASK(
					lp->lwp_thread->td_gd->gd_cpumask),
			    (unsigned long)CPUMASK_LOWMASK(siblings),
			    (unsigned long)CPUMASK_LOWMASK(cpumask));

			goto found;
		}
		++checks;
	}

	/*
	 * Checks exhausted, we tried to defer too many threads, so schedule
	 * the best of the worst.
	 */
	lp = min_level_lwp;
	q = min_q;
	which = min_which;
	pri = min_pri;
	KASSERT(lp, ("chooseproc: at least the first lp was good"));

found:

	/*
	 * If the passed lwp <chklp> is reasonably close to the selected
	 * lwp <lp>, return NULL (indicating that <chklp> should be kept).
	 *
	 * Note that we must error on the side of <chklp> to avoid bouncing
	 * between threads in the acquire code.
	 */
	if (chklp) {
		if (chklp->lwp_priority < lp->lwp_priority + PPQ) {
			bsd4_kick_helper(lp);
			return(NULL);
		}
	}

	KTR_COND_LOG(usched_chooseproc_cc,
	    lp->lwp_proc->p_pid == usched_bsd4_pid_debug,
	    lp->lwp_proc->p_pid,
	    lp->lwp_thread->td_gd->gd_cpuid,
	    mycpu->gd_cpuid);

	TAILQ_REMOVE(q, lp, lwp_procq);
	--bsd4_runqcount;
	if (TAILQ_EMPTY(q))
		*which &= ~(1 << pri);
	KASSERT((lp->lwp_mpflags & LWP_MP_ONRUNQ) != 0, ("not on runq6!"));
	atomic_clear_int(&lp->lwp_mpflags, LWP_MP_ONRUNQ);

	return lp;
}

/*
 * If we aren't willing to schedule a ready process on our cpu, give it's
 * target cpu a kick rather than wait for the next tick.
 *
 * Called with bsd4_spin held.
 */
static
void
bsd4_kick_helper(struct lwp *lp)
{
	globaldata_t gd;
	bsd4_pcpu_t dd;
	cpumask_t tmpmask;

	if (lp == NULL)
		return;
	gd = lp->lwp_thread->td_gd;
	dd = &bsd4_pcpu[gd->gd_cpuid];

	tmpmask = smp_active_mask;
	CPUMASK_ANDMASK(tmpmask, usched_global_cpumask);
	CPUMASK_ANDMASK(tmpmask, bsd4_rdyprocmask);
	CPUMASK_ANDMASK(tmpmask, gd->gd_cpumask);
	if (CPUMASK_TESTZERO(tmpmask))
		return;

	++usched_bsd4_kicks;
	ATOMIC_CPUMASK_NANDBIT(bsd4_rdyprocmask, gd->gd_cpuid);
	if ((dd->upri & ~PPQMASK) > (lp->lwp_priority & ~PPQMASK)) {
		lwkt_send_ipiq(gd, bsd4_need_user_resched_remote, NULL);
	} else {
		wakeup(&dd->helper_thread);
	}
}

static
void
bsd4_need_user_resched_remote(void *dummy)
{
	globaldata_t gd = mycpu;
	bsd4_pcpu_t  dd = &bsd4_pcpu[gd->gd_cpuid];

	need_user_resched();

	/* Call wakeup_mycpu to avoid sending IPIs to other CPUs */
	wakeup_mycpu(&dd->helper_thread);
}

/*
 * bsd4_remrunqueue_locked() removes a given process from the run queue
 * that it is on, clearing the queue busy bit if it becomes empty.
 *
 * Note that user process scheduler is different from the LWKT schedule.
 * The user process scheduler only manages user processes but it uses LWKT
 * underneath, and a user process operating in the kernel will often be
 * 'released' from our management.
 *
 * MPSAFE - bsd4_spin must be held exclusively on call
 */
static void
bsd4_remrunqueue_locked(struct lwp *lp)
{
	struct rq *q;
	u_int32_t *which;
	u_int8_t pri;

	KKASSERT(lp->lwp_mpflags & LWP_MP_ONRUNQ);
	atomic_clear_int(&lp->lwp_mpflags, LWP_MP_ONRUNQ);
	--bsd4_runqcount;
	KKASSERT(bsd4_runqcount >= 0);

	pri = lp->lwp_rqindex;
	switch(lp->lwp_rqtype) {
	case RTP_PRIO_NORMAL:
		q = &bsd4_queues[pri];
		which = &bsd4_queuebits;
		break;
	case RTP_PRIO_REALTIME:
	case RTP_PRIO_FIFO:
		q = &bsd4_rtqueues[pri];
		which = &bsd4_rtqueuebits;
		break;
	case RTP_PRIO_IDLE:
		q = &bsd4_idqueues[pri];
		which = &bsd4_idqueuebits;
		break;
	default:
		panic("remrunqueue: invalid rtprio type");
		/* NOT REACHED */
	}
	TAILQ_REMOVE(q, lp, lwp_procq);
	if (TAILQ_EMPTY(q)) {
		KASSERT((*which & (1 << pri)) != 0,
			("remrunqueue: remove from empty queue"));
		*which &= ~(1 << pri);
	}
}

/*
 * bsd4_setrunqueue_locked()
 *
 * Add a process whos rqtype and rqindex had previously been calculated
 * onto the appropriate run queue.   Determine if the addition requires
 * a reschedule on a cpu and return the cpuid or -1.
 *
 * NOTE: Lower priorities are better priorities.
 *
 * MPSAFE - bsd4_spin must be held exclusively on call
 */
static void
bsd4_setrunqueue_locked(struct lwp *lp)
{
	struct rq *q;
	u_int32_t *which;
	int pri;

	KKASSERT((lp->lwp_mpflags & LWP_MP_ONRUNQ) == 0);
	atomic_set_int(&lp->lwp_mpflags, LWP_MP_ONRUNQ);
	++bsd4_runqcount;

	pri = lp->lwp_rqindex;

	switch(lp->lwp_rqtype) {
	case RTP_PRIO_NORMAL:
		q = &bsd4_queues[pri];
		which = &bsd4_queuebits;
		break;
	case RTP_PRIO_REALTIME:
	case RTP_PRIO_FIFO:
		q = &bsd4_rtqueues[pri];
		which = &bsd4_rtqueuebits;
		break;
	case RTP_PRIO_IDLE:
		q = &bsd4_idqueues[pri];
		which = &bsd4_idqueuebits;
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
	TAILQ_INSERT_TAIL(q, lp, lwp_procq);
	*which |= 1 << pri;
}

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
 *
 * MPSAFE
 */
static void
sched_thread(void *dummy)
{
    globaldata_t gd;
    bsd4_pcpu_t  dd;
    bsd4_pcpu_t  tmpdd;
    struct lwp *nlp;
    cpumask_t mask;
    int cpuid;
    cpumask_t tmpmask;
    int tmpid;

    gd = mycpu;
    cpuid = gd->gd_cpuid;	/* doesn't change */
    mask = gd->gd_cpumask;	/* doesn't change */
    dd = &bsd4_pcpu[cpuid];

    /*
     * Since we are woken up only when no user processes are scheduled
     * on a cpu, we can run at an ultra low priority.
     */
    lwkt_setpri_self(TDPRI_USER_SCHEDULER);

    tsleep(&dd->helper_thread, 0, "sched_thread_sleep", 0);

    for (;;) {
	/*
	 * We use the LWKT deschedule-interlock trick to avoid racing
	 * bsd4_rdyprocmask.  This means we cannot block through to the
	 * manual lwkt_switch() call we make below.
	 */
	crit_enter_gd(gd);
	tsleep_interlock(&dd->helper_thread, 0);
	spin_lock(&bsd4_spin);
	ATOMIC_CPUMASK_ORMASK(bsd4_rdyprocmask, mask);

	clear_user_resched();	/* This satisfied the reschedule request */
	dd->rrcount = 0;	/* Reset the round-robin counter */

	if (CPUMASK_TESTMASK(bsd4_curprocmask, mask) == 0) {
		/*
		 * No thread is currently scheduled.
		 */
		KKASSERT(dd->uschedcp == NULL);
		if ((nlp = bsd4_chooseproc_locked(NULL)) != NULL) {
			KTR_COND_LOG(usched_sched_thread_no_process,
			    nlp->lwp_proc->p_pid == usched_bsd4_pid_debug,
			    gd->gd_cpuid,
			    nlp->lwp_proc->p_pid,
			    nlp->lwp_thread->td_gd->gd_cpuid);

			ATOMIC_CPUMASK_ORMASK(bsd4_curprocmask, mask);
			dd->upri = nlp->lwp_priority;
			dd->uschedcp = nlp;
			dd->rrcount = 0;	/* reset round robin */
			spin_unlock(&bsd4_spin);
			lwkt_acquire(nlp->lwp_thread);
			lwkt_schedule(nlp->lwp_thread);
		} else {
			spin_unlock(&bsd4_spin);
		}
	} else if (bsd4_runqcount) {
		if ((nlp = bsd4_chooseproc_locked(dd->uschedcp)) != NULL) {
			KTR_COND_LOG(usched_sched_thread_process,
			    nlp->lwp_proc->p_pid == usched_bsd4_pid_debug,
			    gd->gd_cpuid,
			    nlp->lwp_proc->p_pid,
			    nlp->lwp_thread->td_gd->gd_cpuid);

			dd->upri = nlp->lwp_priority;
			dd->uschedcp = nlp;
			dd->rrcount = 0;	/* reset round robin */
			spin_unlock(&bsd4_spin);
			lwkt_acquire(nlp->lwp_thread);
			lwkt_schedule(nlp->lwp_thread);
		} else {
			/*
			 * CHAINING CONDITION TRAIN
			 *
			 * We could not deal with the scheduler wakeup
			 * request on this cpu, locate a ready scheduler
			 * with no current lp assignment and chain to it.
			 *
			 * This ensures that a wakeup race which fails due
			 * to priority test does not leave other unscheduled
			 * cpus idle when the runqueue is not empty.
			 */
			tmpmask = bsd4_rdyprocmask;
			CPUMASK_NANDMASK(tmpmask, bsd4_curprocmask);
			CPUMASK_ANDMASK(tmpmask, smp_active_mask);
			if (CPUMASK_TESTNZERO(tmpmask)) {
				tmpid = BSFCPUMASK(tmpmask);
				tmpdd = &bsd4_pcpu[tmpid];
				ATOMIC_CPUMASK_NANDBIT(bsd4_rdyprocmask, tmpid);
				spin_unlock(&bsd4_spin);
				wakeup(&tmpdd->helper_thread);
			} else {
				spin_unlock(&bsd4_spin);
			}

			KTR_LOG(usched_sched_thread_no_process_found,
				gd->gd_cpuid, (unsigned long)CPUMASK_LOWMASK(tmpmask));
		}
	} else {
		/*
		 * The runq is empty.
		 */
		spin_unlock(&bsd4_spin);
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

/* sysctl stick_to_level parameter */
static int
sysctl_usched_bsd4_stick_to_level(SYSCTL_HANDLER_ARGS)
{
	int error, new_val;

	new_val = usched_bsd4_stick_to_level;

	error = sysctl_handle_int(oidp, &new_val, 0, req);
        if (error != 0 || req->newptr == NULL)
		return (error);
	if (new_val > cpu_topology_levels_number - 1 || new_val < 0)
		return (EINVAL);
	usched_bsd4_stick_to_level = new_val;
	return (0);
}

/*
 * Setup our scheduler helpers.  Note that curprocmask bit 0 has already
 * been cleared by rqinit() and we should not mess with it further.
 */
static void
sched_thread_cpu_init(void)
{
	int i;
	int smt_not_supported = 0;
	int cache_coherent_not_supported = 0;

	if (bootverbose)
		kprintf("Start usched_bsd4 helpers on cpus:\n");

	sysctl_ctx_init(&usched_bsd4_sysctl_ctx);
	usched_bsd4_sysctl_tree =
		SYSCTL_ADD_NODE(&usched_bsd4_sysctl_ctx,
				SYSCTL_STATIC_CHILDREN(_kern), OID_AUTO,
				"usched_bsd4", CTLFLAG_RD, 0, "");

	for (i = 0; i < ncpus; ++i) {
		bsd4_pcpu_t dd = &bsd4_pcpu[i];
		cpumask_t mask;

		CPUMASK_ASSBIT(mask, i);

		if (CPUMASK_TESTMASK(mask, smp_active_mask) == 0)
		    continue;

		dd->cpunode = get_cpu_node_by_cpuid(i);

		if (dd->cpunode == NULL) {
			smt_not_supported = 1;
			cache_coherent_not_supported = 1;
			if (bootverbose)
				kprintf ("    cpu%d - WARNING: No CPU NODE "
					 "found for cpu\n", i);
		} else {
			switch (dd->cpunode->type) {
			case THREAD_LEVEL:
				if (bootverbose)
					kprintf ("    cpu%d - HyperThreading "
						 "available. Core siblings: ",
						 i);
				break;
			case CORE_LEVEL:
				smt_not_supported = 1;

				if (bootverbose)
					kprintf ("    cpu%d - No HT available, "
						 "multi-core/physical "
						 "cpu. Physical siblings: ",
						 i);
				break;
			case CHIP_LEVEL:
				smt_not_supported = 1;

				if (bootverbose)
					kprintf ("    cpu%d - No HT available, "
						 "single-core/physical cpu. "
						 "Package Siblings: ",
						 i);
				break;
			default:
				/* Let's go for safe defaults here */
				smt_not_supported = 1;
				cache_coherent_not_supported = 1;
				if (bootverbose)
					kprintf ("    cpu%d - Unknown cpunode->"
						 "type=%u. Siblings: ",
						 i,
						 (u_int)dd->cpunode->type);
				break;
			}

			if (bootverbose) {
				if (dd->cpunode->parent_node != NULL) {
					kprint_cpuset(&dd->cpunode->
						      parent_node->members);
					kprintf("\n");
				} else {
					kprintf(" no siblings\n");
				}
			}
		}

		lwkt_create(sched_thread, NULL, NULL, &dd->helper_thread,
			    0, i, "usched %d", i);

		/*
		 * Allow user scheduling on the target cpu.  cpu #0 has already
		 * been enabled in rqinit().
		 */
		if (i)
			ATOMIC_CPUMASK_NANDMASK(bsd4_curprocmask, mask);
		ATOMIC_CPUMASK_ORMASK(bsd4_rdyprocmask, mask);
		dd->upri = PRIBASE_NULL;

	}

	/* usched_bsd4 sysctl configurable parameters */

	SYSCTL_ADD_INT(&usched_bsd4_sysctl_ctx,
		       SYSCTL_CHILDREN(usched_bsd4_sysctl_tree),
		       OID_AUTO, "rrinterval", CTLFLAG_RW,
		       &usched_bsd4_rrinterval, 0, "");
	SYSCTL_ADD_INT(&usched_bsd4_sysctl_ctx,
		       SYSCTL_CHILDREN(usched_bsd4_sysctl_tree),
		       OID_AUTO, "decay", CTLFLAG_RW,
		       &usched_bsd4_decay, 0, "Extra decay when not running");
	SYSCTL_ADD_INT(&usched_bsd4_sysctl_ctx,
		       SYSCTL_CHILDREN(usched_bsd4_sysctl_tree),
		       OID_AUTO, "batch_time", CTLFLAG_RW,
		       &usched_bsd4_batch_time, 0, "Min batch counter value");
	SYSCTL_ADD_LONG(&usched_bsd4_sysctl_ctx,
		       SYSCTL_CHILDREN(usched_bsd4_sysctl_tree),
		       OID_AUTO, "kicks", CTLFLAG_RW,
		       &usched_bsd4_kicks, "Number of kickstarts");

	/* Add enable/disable option for SMT scheduling if supported */
	if (smt_not_supported) {
		usched_bsd4_smt = 0;
		SYSCTL_ADD_STRING(&usched_bsd4_sysctl_ctx,
				  SYSCTL_CHILDREN(usched_bsd4_sysctl_tree),
				  OID_AUTO, "smt", CTLFLAG_RD,
				  "NOT SUPPORTED", 0, "SMT NOT SUPPORTED");
	} else {
		usched_bsd4_smt = 1;
		SYSCTL_ADD_INT(&usched_bsd4_sysctl_ctx,
			       SYSCTL_CHILDREN(usched_bsd4_sysctl_tree),
			       OID_AUTO, "smt", CTLFLAG_RW,
			       &usched_bsd4_smt, 0, "Enable SMT scheduling");
	}

	/*
	 * Add enable/disable option for cache coherent scheduling
	 * if supported
	 */
	if (cache_coherent_not_supported) {
		usched_bsd4_cache_coherent = 0;
		SYSCTL_ADD_STRING(&usched_bsd4_sysctl_ctx,
				  SYSCTL_CHILDREN(usched_bsd4_sysctl_tree),
				  OID_AUTO, "cache_coherent", CTLFLAG_RD,
				  "NOT SUPPORTED", 0,
				  "Cache coherence NOT SUPPORTED");
	} else {
		usched_bsd4_cache_coherent = 1;
		SYSCTL_ADD_INT(&usched_bsd4_sysctl_ctx,
			       SYSCTL_CHILDREN(usched_bsd4_sysctl_tree),
			       OID_AUTO, "cache_coherent", CTLFLAG_RW,
			       &usched_bsd4_cache_coherent, 0,
			       "Enable/Disable cache coherent scheduling");

		SYSCTL_ADD_INT(&usched_bsd4_sysctl_ctx,
			       SYSCTL_CHILDREN(usched_bsd4_sysctl_tree),
			       OID_AUTO, "upri_affinity", CTLFLAG_RW,
			       &usched_bsd4_upri_affinity, 1,
			       "Number of PPQs in user priority check");

		SYSCTL_ADD_INT(&usched_bsd4_sysctl_ctx,
			       SYSCTL_CHILDREN(usched_bsd4_sysctl_tree),
			       OID_AUTO, "queue_checks", CTLFLAG_RW,
			       &usched_bsd4_queue_checks, 5,
			       "LWPs to check from a queue before giving up");

		SYSCTL_ADD_PROC(&usched_bsd4_sysctl_ctx,
				SYSCTL_CHILDREN(usched_bsd4_sysctl_tree),
				OID_AUTO, "stick_to_level",
				CTLTYPE_INT | CTLFLAG_RW,
				NULL, sizeof usched_bsd4_stick_to_level,
				sysctl_usched_bsd4_stick_to_level, "I",
				"Stick a process to this level. See sysctl"
				"paremter hw.cpu_topology.level_description");
	}
}
SYSINIT(uschedtd, SI_BOOT2_USCHED, SI_ORDER_SECOND,
	sched_thread_cpu_init, NULL)
