/*
 * Copyright (c) 2006 The DragonFly Project.  All rights reserved.
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
#include <machine/cpu.h>
#include <machine/smp.h>

#include <sys/thread2.h>
#include <sys/spinlock2.h>
#include <sys/mplock2.h>

#define MAXPRI			128
#define PRIBASE_REALTIME	0
#define PRIBASE_NORMAL		MAXPRI
#define PRIBASE_IDLE		(MAXPRI * 2)
#define PRIBASE_THREAD		(MAXPRI * 3)
#define PRIBASE_NULL		(MAXPRI * 4)

#define lwp_priority	lwp_usdata.bsd4.priority
#define lwp_estcpu	lwp_usdata.bsd4.estcpu

static void dummy_acquire_curproc(struct lwp *lp);
static void dummy_release_curproc(struct lwp *lp);
static void dummy_select_curproc(globaldata_t gd);
static void dummy_setrunqueue(struct lwp *lp);
static void dummy_schedulerclock(struct lwp *lp, sysclock_t period,
				sysclock_t cpstamp);
static void dummy_recalculate_estcpu(struct lwp *lp);
static void dummy_resetpriority(struct lwp *lp);
static void dummy_forking(struct lwp *plp, struct lwp *lp);
static void dummy_exiting(struct lwp *plp, struct proc *child);
static void dummy_uload_update(struct lwp *lp);
static void dummy_yield(struct lwp *lp);
static void dummy_changedcpu(struct lwp *lp);

struct usched usched_dummy = {
	{ NULL },
	"dummy", "Dummy DragonFly Scheduler",
	NULL,			/* default registration */
	NULL,			/* default deregistration */
	dummy_acquire_curproc,
	dummy_release_curproc,
	dummy_setrunqueue,
	dummy_schedulerclock,
	dummy_recalculate_estcpu,
	dummy_resetpriority,
	dummy_forking,
	dummy_exiting,
	dummy_uload_update,
	NULL,			/* setcpumask not supported */
	dummy_yield,
	dummy_changedcpu
};

struct usched_dummy_pcpu {
	int	rrcount;
	struct thread helper_thread;
	struct lwp *uschedcp;
};

typedef struct usched_dummy_pcpu *dummy_pcpu_t;

static struct usched_dummy_pcpu dummy_pcpu[MAXCPU];
static cpumask_t dummy_curprocmask = CPUMASK_INITIALIZER_ALLONES;
static cpumask_t dummy_rdyprocmask;
static struct spinlock dummy_spin;
static TAILQ_HEAD(rq, lwp) dummy_runq;
static int dummy_runqcount;

static int usched_dummy_rrinterval = (ESTCPUFREQ + 9) / 10;
SYSCTL_INT(_kern, OID_AUTO, usched_dummy_rrinterval, CTLFLAG_RW,
        &usched_dummy_rrinterval, 0, "");

/*
 * Initialize the run queues at boot time, clear cpu 0 in curprocmask
 * to allow dummy scheduling on cpu 0.
 */
static void
dummyinit(void *dummy)
{
	TAILQ_INIT(&dummy_runq);
	spin_init(&dummy_spin);
	ATOMIC_CPUMASK_NANDBIT(dummy_curprocmask, 0);
}
SYSINIT(runqueue, SI_BOOT2_USCHED, SI_ORDER_FIRST, dummyinit, NULL)

/*
 * DUMMY_ACQUIRE_CURPROC
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
 * We are expected to handle userland reschedule requests here too.
 *
 * WARNING! THIS FUNCTION IS ALLOWED TO CAUSE THE CURRENT THREAD TO MIGRATE
 * TO ANOTHER CPU!  Because most of the kernel assumes that no migration will
 * occur, this function is called only under very controlled circumstances.
 *
 * MPSAFE
 */
static void
dummy_acquire_curproc(struct lwp *lp)
{
	globaldata_t gd = mycpu;
	dummy_pcpu_t dd = &dummy_pcpu[gd->gd_cpuid];
	thread_t td = lp->lwp_thread;

	/*
	 * Possibly select another thread
	 */
	if (user_resched_wanted())
		dummy_select_curproc(gd);

	/*
	 * If this cpu has no current thread, select ourself
	 */
	if (dd->uschedcp == lp ||
	    (dd->uschedcp == NULL && TAILQ_EMPTY(&dummy_runq))) {
		ATOMIC_CPUMASK_ORBIT(dummy_curprocmask, gd->gd_cpuid);
		dd->uschedcp = lp;
		return;
	}

	/*
	 * If this cpu's current user process thread is not our thread,
	 * deschedule ourselves and place us on the run queue, then
	 * switch away.
	 *
	 * We loop until we become the current process.  Its a good idea
	 * to run any passive release(s) before we mess with the scheduler
	 * so our thread is in the expected state.
	 */
	KKASSERT(dd->uschedcp != lp);
	if (td->td_release)
		td->td_release(lp->lwp_thread);
	do {
		crit_enter();
		lwkt_deschedule_self(td);
		dummy_setrunqueue(lp);
		if ((td->td_flags & TDF_RUNQ) == 0)
			++lp->lwp_ru.ru_nivcsw;
		lwkt_switch();		/* WE MAY MIGRATE TO ANOTHER CPU */
		crit_exit();
		gd = mycpu;
		dd = &dummy_pcpu[gd->gd_cpuid];
		KKASSERT((lp->lwp_mpflags & LWP_MP_ONRUNQ) == 0);
	} while (dd->uschedcp != lp);
}

/*
 * DUMMY_RELEASE_CURPROC
 *
 * This routine detaches the current thread from the userland scheduler,
 * usually because the thread needs to run in the kernel (at kernel priority)
 * for a while.
 *
 * This routine is also responsible for selecting a new thread to
 * make the current thread.
 *
 * MPSAFE
 */
static void
dummy_release_curproc(struct lwp *lp)
{
	globaldata_t gd = mycpu;
	dummy_pcpu_t dd = &dummy_pcpu[gd->gd_cpuid];

	KKASSERT((lp->lwp_mpflags & LWP_MP_ONRUNQ) == 0);
	if (dd->uschedcp == lp) {
		dummy_select_curproc(gd);
	}
}

/*
 * DUMMY_SELECT_CURPROC
 *
 * Select a new current process for this cpu.  This satisfies a user
 * scheduler reschedule request so clear that too.
 *
 * This routine is also responsible for equal-priority round-robining,
 * typically triggered from dummy_schedulerclock().  In our dummy example
 * all the 'user' threads are LWKT scheduled all at once and we just
 * call lwkt_switch().
 *
 * MPSAFE
 */
static
void
dummy_select_curproc(globaldata_t gd)
{
	dummy_pcpu_t dd = &dummy_pcpu[gd->gd_cpuid];
	struct lwp *lp;

	clear_user_resched();
	spin_lock(&dummy_spin);
	if ((lp = TAILQ_FIRST(&dummy_runq)) == NULL) {
		dd->uschedcp = NULL;
		ATOMIC_CPUMASK_NANDBIT(dummy_curprocmask, gd->gd_cpuid);
		spin_unlock(&dummy_spin);
	} else {
		--dummy_runqcount;
		TAILQ_REMOVE(&dummy_runq, lp, lwp_procq);
		atomic_clear_int(&lp->lwp_mpflags, LWP_MP_ONRUNQ);
		dd->uschedcp = lp;
		ATOMIC_CPUMASK_ORBIT(dummy_curprocmask, gd->gd_cpuid);
		spin_unlock(&dummy_spin);
		lwkt_acquire(lp->lwp_thread);
		lwkt_schedule(lp->lwp_thread);
	}
}

/*
 * DUMMY_SETRUNQUEUE
 *
 * This routine is called to schedule a new user process after a fork.
 * The scheduler module itself might also call this routine to place
 * the current process on the userland scheduler's run queue prior
 * to calling dummy_select_curproc().
 *
 * The caller may set LWP_PASSIVE_ACQ in lwp_flags to indicate that we should
 * attempt to leave the thread on the current cpu.
 *
 * MPSAFE
 */
static void
dummy_setrunqueue(struct lwp *lp)
{
	globaldata_t gd = mycpu;
	dummy_pcpu_t dd = &dummy_pcpu[gd->gd_cpuid];
	cpumask_t mask;
	int cpuid;

	if (dd->uschedcp == NULL) {
		dd->uschedcp = lp;
		ATOMIC_CPUMASK_ORBIT(dummy_curprocmask, gd->gd_cpuid);
		lwkt_schedule(lp->lwp_thread);
	} else {
		/*
		 * Add to our global runq
		 */
		KKASSERT((lp->lwp_mpflags & LWP_MP_ONRUNQ) == 0);
		spin_lock(&dummy_spin);
		++dummy_runqcount;
		TAILQ_INSERT_TAIL(&dummy_runq, lp, lwp_procq);
		atomic_set_int(&lp->lwp_mpflags, LWP_MP_ONRUNQ);
		lwkt_giveaway(lp->lwp_thread);

		/* lp = TAILQ_FIRST(&dummy_runq); */

		/*
		 * Notify the next available cpu.  P.S. some
		 * cpu affinity could be done here.
		 *
		 * The rdyprocmask bit placeholds the knowledge that there
		 * is a process on the runq that needs service.  If the
		 * helper thread cannot find a home for it it will forward
		 * the request to another available cpu.
		 */
		mask = dummy_rdyprocmask;
		CPUMASK_NANDMASK(mask, dummy_curprocmask);
		CPUMASK_ANDMASK(mask, gd->gd_other_cpus);
		if (CPUMASK_TESTNZERO(mask)) {
			cpuid = BSFCPUMASK(mask);
			ATOMIC_CPUMASK_NANDBIT(dummy_rdyprocmask, cpuid);
			spin_unlock(&dummy_spin);
			lwkt_schedule(&dummy_pcpu[cpuid].helper_thread);
		} else {
			spin_unlock(&dummy_spin);
		}
	}
}

/*
 * This routine is called from a systimer IPI.  It must NEVER block.
 * If a lwp compatible with this scheduler is the currently running
 * thread this function is called with a non-NULL lp, otherwise it
 * will be called with a NULL lp.
 *
 * This routine is called at ESTCPUFREQ on each cpu independantly.
 *
 * This routine typically queues a reschedule request, which will cause
 * the scheduler's BLAH_select_curproc() to be called as soon as possible.
 */
static
void
dummy_schedulerclock(struct lwp *lp, sysclock_t period, sysclock_t cpstamp)
{
	globaldata_t gd = mycpu;
	dummy_pcpu_t dd = &dummy_pcpu[gd->gd_cpuid];

	if (lp == NULL)
		return;

	if (++dd->rrcount >= usched_dummy_rrinterval) {
		dd->rrcount = 0;
		need_user_resched();
	}
}

/*
 * DUMMY_RECALCULATE_ESTCPU
 *
 * Called once a second for any process that is running or has slept
 * for less then 2 seconds.
 *
 * MPSAFE
 */
static
void 
dummy_recalculate_estcpu(struct lwp *lp)
{
}

/*
 * MPSAFE
 */
static
void
dummy_yield(struct lwp *lp)
{
	need_user_resched();
}

static
void
dummy_changedcpu(struct lwp *lp __unused)
{
}

/*
 * DUMMY_RESETPRIORITY
 *
 * This routine is called after the kernel has potentially modified
 * the lwp_rtprio structure.  The target process may be running or sleeping
 * or scheduled but not yet running or owned by another cpu.  Basically,
 * it can be in virtually any state.
 *
 * This routine is called by fork1() for initial setup with the process 
 * of the run queue, and also may be called normally with the process on or
 * off the run queue.
 *
 * MPSAFE
 */
static void
dummy_resetpriority(struct lwp *lp)
{
	/* XXX spinlock usually needed */
	/*
	 * Set p_priority for general process comparisons
	 */
	switch(lp->lwp_rtprio.type) {
	case RTP_PRIO_REALTIME:
		lp->lwp_priority = PRIBASE_REALTIME + lp->lwp_rtprio.prio;
		return;
	case RTP_PRIO_NORMAL:
		lp->lwp_priority = PRIBASE_NORMAL + lp->lwp_rtprio.prio;
		break;
	case RTP_PRIO_IDLE:
		lp->lwp_priority = PRIBASE_IDLE + lp->lwp_rtprio.prio;
		return;
	case RTP_PRIO_THREAD:
		lp->lwp_priority = PRIBASE_THREAD + lp->lwp_rtprio.prio;
		return;
	}

	/*
	 * td_upri has normal sense (higher numbers are more desireable),
	 * so negate it.
	 */
	lp->lwp_thread->td_upri = -lp->lwp_priority;
	/* XXX spinlock usually needed */
}


/*
 * DUMMY_FORKING
 *
 * Called from fork1() when a new child process is being created.  Allows
 * the scheduler to predispose the child process before it gets scheduled.
 *
 * MPSAFE
 */
static void
dummy_forking(struct lwp *plp, struct lwp *lp)
{
	lp->lwp_estcpu = plp->lwp_estcpu;
#if 0
	++plp->lwp_estcpu;
#endif
}

/*
 * Called when a lwp is being removed from this scheduler, typically
 * during lwp_exit().
 */
static void
dummy_exiting(struct lwp *plp, struct proc *child)
{
}

static void
dummy_uload_update(struct lwp *lp)
{
}

/*
 * SMP systems may need a scheduler helper thread.  This is how one can be
 * setup.
 *
 * We use a neat LWKT scheduling trick to interlock the helper thread.  It
 * is possible to deschedule an LWKT thread and then do some work before
 * switching away.  The thread can be rescheduled at any time, even before
 * we switch away.
 *
 * MPSAFE
 */
static void
dummy_sched_thread(void *dummy)
{
    globaldata_t gd;
    dummy_pcpu_t dd;
    struct lwp *lp;
    cpumask_t cpumask;
    cpumask_t tmpmask;
    int cpuid;
    int tmpid;

    gd = mycpu;
    cpuid = gd->gd_cpuid;
    dd = &dummy_pcpu[cpuid];
    CPUMASK_ASSBIT(cpumask, cpuid);

    for (;;) {
	lwkt_deschedule_self(gd->gd_curthread);		/* interlock */
	ATOMIC_CPUMASK_ORBIT(dummy_rdyprocmask, cpuid);
	spin_lock(&dummy_spin);
	if (dd->uschedcp) {
		/*
		 * We raced another cpu trying to schedule a thread onto us.
		 * If the runq isn't empty hit another free cpu.
		 */
		tmpmask = dummy_rdyprocmask;
		CPUMASK_NANDMASK(tmpmask, dummy_curprocmask);
		CPUMASK_ANDMASK(tmpmask, gd->gd_other_cpus);
		if (CPUMASK_TESTNZERO(tmpmask) && dummy_runqcount) {
			tmpid = BSFCPUMASK(tmpmask);
			KKASSERT(tmpid != cpuid);
			ATOMIC_CPUMASK_NANDBIT(dummy_rdyprocmask, tmpid);
			spin_unlock(&dummy_spin);
			lwkt_schedule(&dummy_pcpu[tmpid].helper_thread);
		} else {
			spin_unlock(&dummy_spin);
		}
	} else if ((lp = TAILQ_FIRST(&dummy_runq)) != NULL) {
		--dummy_runqcount;
		TAILQ_REMOVE(&dummy_runq, lp, lwp_procq);
		atomic_clear_int(&lp->lwp_mpflags, LWP_MP_ONRUNQ);
		dd->uschedcp = lp;
		ATOMIC_CPUMASK_ORBIT(dummy_curprocmask, cpuid);
		spin_unlock(&dummy_spin);
		lwkt_acquire(lp->lwp_thread);
		lwkt_schedule(lp->lwp_thread);
	} else {
		spin_unlock(&dummy_spin);
	}
	lwkt_switch();
    }
}

/*
 * Setup our scheduler helpers.  Note that curprocmask bit 0 has already
 * been cleared by rqinit() and we should not mess with it further.
 */
static void
dummy_sched_thread_cpu_init(void)
{
    int i;

    if (bootverbose)
	kprintf("start dummy scheduler helpers on cpus:");

    for (i = 0; i < ncpus; ++i) {
	dummy_pcpu_t dd = &dummy_pcpu[i];
	cpumask_t mask;

	CPUMASK_ASSBIT(mask, i);

	if (CPUMASK_TESTMASK(mask, smp_active_mask) == 0)
	    continue;

	if (bootverbose)
	    kprintf(" %d", i);

	lwkt_create(dummy_sched_thread, NULL, NULL, &dd->helper_thread, 
		    TDF_NOSTART, i, "dsched %d", i);

	/*
	 * Allow user scheduling on the target cpu.  cpu #0 has already
	 * been enabled in rqinit().
	 */
	if (i)
		ATOMIC_CPUMASK_NANDMASK(dummy_curprocmask, mask);
	ATOMIC_CPUMASK_ORMASK(dummy_rdyprocmask, mask);
    }
    if (bootverbose)
	kprintf("\n");
}
SYSINIT(uschedtd, SI_BOOT2_USCHED, SI_ORDER_SECOND,
	dummy_sched_thread_cpu_init, NULL)
