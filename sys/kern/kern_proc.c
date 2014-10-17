/*
 * Copyright (c) 1982, 1986, 1989, 1991, 1993
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
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/kernel.h>
#include <sys/sysctl.h>
#include <sys/malloc.h>
#include <sys/proc.h>
#include <sys/vnode.h>
#include <sys/jail.h>
#include <sys/filedesc.h>
#include <sys/tty.h>
#include <sys/dsched.h>
#include <sys/signalvar.h>
#include <sys/spinlock.h>
#include <sys/random.h>
#include <sys/vnode.h>
#include <vm/vm.h>
#include <sys/lock.h>
#include <vm/pmap.h>
#include <vm/vm_map.h>
#include <sys/user.h>
#include <machine/smp.h>

#include <sys/refcount.h>
#include <sys/spinlock2.h>
#include <sys/mplock2.h>

/*
 * Hash table size must be a power of two and is not currently dynamically
 * sized.  There is a trade-off between the linear scans which must iterate
 * all HSIZE elements and the number of elements which might accumulate
 * within each hash chain.
 */
#define ALLPROC_HSIZE	256
#define ALLPROC_HMASK	(ALLPROC_HSIZE - 1)
#define ALLPROC_HASH(pid)	(pid & ALLPROC_HMASK)
#define PGRP_HASH(pid)	(pid & ALLPROC_HMASK)
#define SESS_HASH(pid)	(pid & ALLPROC_HMASK)

/*
 * pid_doms[] management, used to control how quickly a PID can be recycled.
 * Must be a multiple of ALLPROC_HSIZE for the proc_makepid() inner loops.
 *
 * WARNING! PIDDOM_DELAY should not be defined > 20 or so unless you change
 *	    the array from int8_t's to int16_t's.
 */
#define PIDDOM_COUNT	10	/* 10 pids per domain - reduce array size */
#define PIDDOM_DELAY	10	/* min 10 seconds after exit before reuse */
#define PIDSEL_DOMAINS	(PID_MAX / PIDDOM_COUNT / ALLPROC_HSIZE * ALLPROC_HSIZE)

/* Used by libkvm */
int allproc_hsize = ALLPROC_HSIZE;

LIST_HEAD(pidhashhead, proc);

static MALLOC_DEFINE(M_PGRP, "pgrp", "process group header");
MALLOC_DEFINE(M_SESSION, "session", "session header");
MALLOC_DEFINE(M_PROC, "proc", "Proc structures");
MALLOC_DEFINE(M_LWP, "lwp", "lwp structures");
MALLOC_DEFINE(M_SUBPROC, "subproc", "Proc sub-structures");

int ps_showallprocs = 1;
static int ps_showallthreads = 1;
SYSCTL_INT(_security, OID_AUTO, ps_showallprocs, CTLFLAG_RW,
    &ps_showallprocs, 0,
    "Unprivileged processes can see processes with different UID/GID");
SYSCTL_INT(_security, OID_AUTO, ps_showallthreads, CTLFLAG_RW,
    &ps_showallthreads, 0,
    "Unprivileged processes can see kernel threads");
static u_int pid_domain_skips;
SYSCTL_UINT(_kern, OID_AUTO, pid_domain_skips, CTLFLAG_RW,
    &pid_domain_skips, 0,
    "Number of pid_doms[] skipped");
static u_int pid_inner_skips;
SYSCTL_UINT(_kern, OID_AUTO, pid_inner_skips, CTLFLAG_RW,
    &pid_inner_skips, 0,
    "Number of pid_doms[] skipped");

static void orphanpg(struct pgrp *pg);
static void proc_makepid(struct proc *p, int random_offset);

/*
 * Other process lists
 */
static struct lwkt_token proc_tokens[ALLPROC_HSIZE];
static struct proclist allprocs[ALLPROC_HSIZE];	/* locked by proc_tokens */
static struct pgrplist allpgrps[ALLPROC_HSIZE];	/* locked by proc_tokens */
static struct sesslist allsessn[ALLPROC_HSIZE];	/* locked by proc_tokens */

/*
 * We try our best to avoid recycling a PID too quickly.  We do this by
 * storing (uint8_t)time_second in the related pid domain on-reap and then
 * using that to skip-over the domain on-allocate.
 *
 * This array has to be fairly large to support a high fork/exec rate.
 * We want ~100,000 entries or so to support a 10-second reuse latency
 * at 10,000 execs/second, worst case.  Best-case multiply by PIDDOM_COUNT
 * (approximately 100,000 execs/second).
 */
static uint8_t pid_doms[PIDSEL_DOMAINS];	/* ~100,000 entries */

/*
 * Random component to nextpid generation.  We mix in a random factor to make
 * it a little harder to predict.  We sanity check the modulus value to avoid
 * doing it in critical paths.  Don't let it be too small or we pointlessly
 * waste randomness entropy, and don't let it be impossibly large.  Using a
 * modulus that is too big causes a LOT more process table scans and slows
 * down fork processing as the pidchecked caching is defeated.
 */
static int randompid = 0;

/*
 * No requirements.
 */
static int
sysctl_kern_randompid(SYSCTL_HANDLER_ARGS)
{
	int error, pid;

	pid = randompid;
	error = sysctl_handle_int(oidp, &pid, 0, req);
	if (error || !req->newptr)
		return (error);
	if (pid < 0 || pid > PID_MAX - 100)     /* out of range */
		pid = PID_MAX - 100;
	else if (pid < 2)                       /* NOP */
		pid = 0;
	else if (pid < 100)                     /* Make it reasonable */
		pid = 100;
	randompid = pid;
	return (error);
}

SYSCTL_PROC(_kern, OID_AUTO, randompid, CTLTYPE_INT|CTLFLAG_RW,
	    0, 0, sysctl_kern_randompid, "I", "Random PID modulus");

/*
 * Initialize global process hashing structures.
 *
 * These functions are ONLY called from the low level boot code and do
 * not lock their operations.
 */
void
procinit(void)
{
	u_long i;

	/*
	 * Avoid unnecessary stalls due to pid_doms[] values all being
	 * the same.  Make sure that the allocation of pid 1 and pid 2
	 * succeeds.
	 */
	for (i = 0; i < PIDSEL_DOMAINS; ++i)
		pid_doms[i] = (int8_t)i - (int8_t)(PIDDOM_DELAY + 1);

	/*
	 * Other misc init.
	 */
	for (i = 0; i < ALLPROC_HSIZE; ++i) {
		LIST_INIT(&allprocs[i]);
		LIST_INIT(&allsessn[i]);
		LIST_INIT(&allpgrps[i]);
		lwkt_token_init(&proc_tokens[i], "allproc");
	}
	lwkt_init();
	uihashinit();
}

void
procinsertinit(struct proc *p)
{
	LIST_INSERT_HEAD(&allprocs[ALLPROC_HASH(p->p_pid)], p, p_list);
}

void
pgrpinsertinit(struct pgrp *pg)
{
	LIST_INSERT_HEAD(&allpgrps[ALLPROC_HASH(pg->pg_id)], pg, pg_list);
}

void
sessinsertinit(struct session *sess)
{
	LIST_INSERT_HEAD(&allsessn[ALLPROC_HASH(sess->s_sid)], sess, s_list);
}

/*
 * Process hold/release support functions.  Called via the PHOLD(),
 * PRELE(), and PSTALL() macros.
 *
 * p->p_lock is a simple hold count with a waiting interlock.  No wakeup()
 * is issued unless someone is actually waiting for the process.
 *
 * Most holds are short-term, allowing a process scan or other similar
 * operation to access a proc structure without it getting ripped out from
 * under us.  procfs and process-list sysctl ops also use the hold function
 * interlocked with various p_flags to keep the vmspace intact when reading
 * or writing a user process's address space.
 *
 * There are two situations where a hold count can be longer.  Exiting lwps
 * hold the process until the lwp is reaped, and the parent will hold the
 * child during vfork()/exec() sequences while the child is marked P_PPWAIT.
 *
 * The kernel waits for the hold count to drop to 0 (or 1 in some cases) at
 * various critical points in the fork/exec and exit paths before proceeding.
 */
#define PLOCK_ZOMB	0x20000000
#define PLOCK_WAITING	0x40000000
#define PLOCK_MASK	0x1FFFFFFF

void
pstall(struct proc *p, const char *wmesg, int count)
{
	int o;
	int n;

	for (;;) {
		o = p->p_lock;
		cpu_ccfence();
		if ((o & PLOCK_MASK) <= count)
			break;
		n = o | PLOCK_WAITING;
		tsleep_interlock(&p->p_lock, 0);

		/*
		 * If someone is trying to single-step the process during
		 * an exec or an exit they can deadlock us because procfs
		 * sleeps with the process held.
		 */
		if (p->p_stops) {
			if (p->p_flags & P_INEXEC) {
				wakeup(&p->p_stype);
			} else if (p->p_flags & P_POSTEXIT) {
				spin_lock(&p->p_spin);
				p->p_stops = 0;
				p->p_step = 0;
				spin_unlock(&p->p_spin);
				wakeup(&p->p_stype);
			}
		}

		if (atomic_cmpset_int(&p->p_lock, o, n)) {
			tsleep(&p->p_lock, PINTERLOCKED, wmesg, 0);
		}
	}
}

void
phold(struct proc *p)
{
	atomic_add_int(&p->p_lock, 1);
}

/*
 * WARNING!  On last release (p) can become instantly invalid due to
 *	     MP races.
 */
void
prele(struct proc *p)
{
	int o;
	int n;

	/*
	 * Fast path
	 */
	if (atomic_cmpset_int(&p->p_lock, 1, 0))
		return;

	/*
	 * Slow path
	 */
	for (;;) {
		o = p->p_lock;
		KKASSERT((o & PLOCK_MASK) > 0);
		cpu_ccfence();
		n = (o - 1) & ~PLOCK_WAITING;
		if (atomic_cmpset_int(&p->p_lock, o, n)) {
			if (o & PLOCK_WAITING)
				wakeup(&p->p_lock);
			break;
		}
	}
}

/*
 * Hold and flag serialized for zombie reaping purposes.
 *
 * This function will fail if it has to block, returning non-zero with
 * neither the flag set or the hold count bumped.  Note that we must block
 * without holding a ref, meaning that the caller must ensure that (p)
 * remains valid through some other interlock (typically on its parent
 * process's p_token).
 *
 * Zero is returned on success.  The hold count will be incremented and
 * the serialization flag acquired.  Note that serialization is only against
 * other pholdzomb() calls, not against phold() calls.
 */
int
pholdzomb(struct proc *p)
{
	int o;
	int n;

	/*
	 * Fast path
	 */
	if (atomic_cmpset_int(&p->p_lock, 0, PLOCK_ZOMB | 1))
		return(0);

	/*
	 * Slow path
	 */
	for (;;) {
		o = p->p_lock;
		cpu_ccfence();
		if ((o & PLOCK_ZOMB) == 0) {
			n = (o + 1) | PLOCK_ZOMB;
			if (atomic_cmpset_int(&p->p_lock, o, n))
				return(0);
		} else {
			KKASSERT((o & PLOCK_MASK) > 0);
			n = o | PLOCK_WAITING;
			tsleep_interlock(&p->p_lock, 0);
			if (atomic_cmpset_int(&p->p_lock, o, n)) {
				tsleep(&p->p_lock, PINTERLOCKED, "phldz", 0);
				/* (p) can be ripped out at this point */
				return(1);
			}
		}
	}
}

/*
 * Release PLOCK_ZOMB and the hold count, waking up any waiters.
 *
 * WARNING!  On last release (p) can become instantly invalid due to
 *	     MP races.
 */
void
prelezomb(struct proc *p)
{
	int o;
	int n;

	/*
	 * Fast path
	 */
	if (atomic_cmpset_int(&p->p_lock, PLOCK_ZOMB | 1, 0))
		return;

	/*
	 * Slow path
	 */
	KKASSERT(p->p_lock & PLOCK_ZOMB);
	for (;;) {
		o = p->p_lock;
		KKASSERT((o & PLOCK_MASK) > 0);
		cpu_ccfence();
		n = (o - 1) & ~(PLOCK_ZOMB | PLOCK_WAITING);
		if (atomic_cmpset_int(&p->p_lock, o, n)) {
			if (o & PLOCK_WAITING)
				wakeup(&p->p_lock);
			break;
		}
	}
}

/*
 * Is p an inferior of the current process?
 *
 * No requirements.
 */
int
inferior(struct proc *p)
{
	struct proc *p2;

	PHOLD(p);
	lwkt_gettoken_shared(&p->p_token);
	while (p != curproc) {
		if (p->p_pid == 0) {
			lwkt_reltoken(&p->p_token);
			return (0);
		}
		p2 = p->p_pptr;
		PHOLD(p2);
		lwkt_reltoken(&p->p_token);
		PRELE(p);
		lwkt_gettoken_shared(&p2->p_token);
		p = p2;
	}
	lwkt_reltoken(&p->p_token);
	PRELE(p);

	return (1);
}

/*
 * Locate a process by number.  The returned process will be referenced and
 * must be released with PRELE().
 *
 * No requirements.
 */
struct proc *
pfind(pid_t pid)
{
	struct proc *p = curproc;
	int n;

	/*
	 * Shortcut the current process
	 */
	if (p && p->p_pid == pid) {
		PHOLD(p);
		return (p);
	}

	/*
	 * Otherwise find it in the hash table.
	 */
	n = ALLPROC_HASH(pid);

	lwkt_gettoken_shared(&proc_tokens[n]);
	LIST_FOREACH(p, &allprocs[n], p_list) {
		if (p->p_stat == SZOMB)
			continue;
		if (p->p_pid == pid) {
			PHOLD(p);
			lwkt_reltoken(&proc_tokens[n]);
			return (p);
		}
	}
	lwkt_reltoken(&proc_tokens[n]);

	return (NULL);
}

/*
 * Locate a process by number.  The returned process is NOT referenced.
 * The result will not be stable and is typically only used to validate
 * against a process that the caller has in-hand.
 *
 * No requirements.
 */
struct proc *
pfindn(pid_t pid)
{
	struct proc *p = curproc;
	int n;

	/*
	 * Shortcut the current process
	 */
	if (p && p->p_pid == pid)
		return (p);

	/*
	 * Otherwise find it in the hash table.
	 */
	n = ALLPROC_HASH(pid);

	lwkt_gettoken_shared(&proc_tokens[n]);
	LIST_FOREACH(p, &allprocs[n], p_list) {
		if (p->p_stat == SZOMB)
			continue;
		if (p->p_pid == pid) {
			lwkt_reltoken(&proc_tokens[n]);
			return (p);
		}
	}
	lwkt_reltoken(&proc_tokens[n]);

	return (NULL);
}

/*
 * Locate a process on the zombie list.  Return a process or NULL.
 * The returned process will be referenced and the caller must release
 * it with PRELE().
 *
 * No other requirements.
 */
struct proc *
zpfind(pid_t pid)
{
	struct proc *p = curproc;
	int n;

	/*
	 * Shortcut the current process
	 */
	if (p && p->p_pid == pid) {
		PHOLD(p);
		return (p);
	}

	/*
	 * Otherwise find it in the hash table.
	 */
	n = ALLPROC_HASH(pid);

	lwkt_gettoken_shared(&proc_tokens[n]);
	LIST_FOREACH(p, &allprocs[n], p_list) {
		if (p->p_stat != SZOMB)
			continue;
		if (p->p_pid == pid) {
			PHOLD(p);
			lwkt_reltoken(&proc_tokens[n]);
			return (p);
		}
	}
	lwkt_reltoken(&proc_tokens[n]);

	return (NULL);
}


void
pgref(struct pgrp *pgrp)
{
	refcount_acquire(&pgrp->pg_refs);
}

void
pgrel(struct pgrp *pgrp)
{
	int count;
	int n;

	n = PGRP_HASH(pgrp->pg_id);
	for (;;) {
		count = pgrp->pg_refs;
		cpu_ccfence();
		KKASSERT(count > 0);
		if (count == 1) {
			lwkt_gettoken(&proc_tokens[n]);
			if (atomic_cmpset_int(&pgrp->pg_refs, 1, 0))
				break;
			lwkt_reltoken(&proc_tokens[n]);
			/* retry */
		} else {
			if (atomic_cmpset_int(&pgrp->pg_refs, count, count - 1))
				return;
			/* retry */
		}
	}

	/*
	 * Successful 1->0 transition, pghash_spin is held.
	 */
	LIST_REMOVE(pgrp, pg_list);
	pid_doms[pgrp->pg_id % PIDSEL_DOMAINS] = (uint8_t)time_second;

	/*
	 * Reset any sigio structures pointing to us as a result of
	 * F_SETOWN with our pgid.
	 */
	funsetownlst(&pgrp->pg_sigiolst);

	if (pgrp->pg_session->s_ttyp != NULL &&
	    pgrp->pg_session->s_ttyp->t_pgrp == pgrp) {
		pgrp->pg_session->s_ttyp->t_pgrp = NULL;
	}
	lwkt_reltoken(&proc_tokens[n]);

	sess_rele(pgrp->pg_session);
	kfree(pgrp, M_PGRP);
}

/*
 * Locate a process group by number.  The returned process group will be
 * referenced w/pgref() and must be released with pgrel() (or assigned
 * somewhere if you wish to keep the reference).
 *
 * No requirements.
 */
struct pgrp *
pgfind(pid_t pgid)
{
	struct pgrp *pgrp;
	int n;

	n = PGRP_HASH(pgid);
	lwkt_gettoken_shared(&proc_tokens[n]);

	LIST_FOREACH(pgrp, &allpgrps[n], pg_list) {
		if (pgrp->pg_id == pgid) {
			refcount_acquire(&pgrp->pg_refs);
			lwkt_reltoken(&proc_tokens[n]);
			return (pgrp);
		}
	}
	lwkt_reltoken(&proc_tokens[n]);
	return (NULL);
}

/*
 * Move p to a new or existing process group (and session)
 *
 * No requirements.
 */
int
enterpgrp(struct proc *p, pid_t pgid, int mksess)
{
	struct pgrp *pgrp;
	struct pgrp *opgrp;
	int error;

	pgrp = pgfind(pgid);

	KASSERT(pgrp == NULL || !mksess,
		("enterpgrp: setsid into non-empty pgrp"));
	KASSERT(!SESS_LEADER(p),
		("enterpgrp: session leader attempted setpgrp"));

	if (pgrp == NULL) {
		pid_t savepid = p->p_pid;
		struct proc *np;
		int n;

		/*
		 * new process group
		 */
		KASSERT(p->p_pid == pgid,
			("enterpgrp: new pgrp and pid != pgid"));
		pgrp = kmalloc(sizeof(struct pgrp), M_PGRP, M_WAITOK | M_ZERO);
		pgrp->pg_id = pgid;
		LIST_INIT(&pgrp->pg_members);
		pgrp->pg_jobc = 0;
		SLIST_INIT(&pgrp->pg_sigiolst);
		lwkt_token_init(&pgrp->pg_token, "pgrp_token");
		refcount_init(&pgrp->pg_refs, 1);
		lockinit(&pgrp->pg_lock, "pgwt", 0, 0);

		n = PGRP_HASH(pgid);

		if ((np = pfindn(savepid)) == NULL || np != p) {
			lwkt_reltoken(&proc_tokens[n]);
			error = ESRCH;
			kfree(pgrp, M_PGRP);
			goto fatal;
		}

		lwkt_gettoken(&proc_tokens[n]);
		if (mksess) {
			struct session *sess;

			/*
			 * new session
			 */
			sess = kmalloc(sizeof(struct session), M_SESSION,
				       M_WAITOK | M_ZERO);
			lwkt_gettoken(&p->p_token);
			sess->s_leader = p;
			sess->s_sid = p->p_pid;
			sess->s_count = 1;
			sess->s_ttyvp = NULL;
			sess->s_ttyp = NULL;
			bcopy(p->p_session->s_login, sess->s_login,
			      sizeof(sess->s_login));
			pgrp->pg_session = sess;
			KASSERT(p == curproc,
				("enterpgrp: mksession and p != curproc"));
			p->p_flags &= ~P_CONTROLT;
			LIST_INSERT_HEAD(&allsessn[n], sess, s_list);
			lwkt_reltoken(&p->p_token);
		} else {
			lwkt_gettoken(&p->p_token);
			pgrp->pg_session = p->p_session;
			sess_hold(pgrp->pg_session);
			lwkt_reltoken(&p->p_token);
		}
		LIST_INSERT_HEAD(&allpgrps[n], pgrp, pg_list);

		lwkt_reltoken(&proc_tokens[n]);
	} else if (pgrp == p->p_pgrp) {
		pgrel(pgrp);
		goto done;
	} /* else pgfind() referenced the pgrp */

	lwkt_gettoken(&pgrp->pg_token);
	lwkt_gettoken(&p->p_token);

	/*
	 * Replace p->p_pgrp, handling any races that occur.
	 */
	while ((opgrp = p->p_pgrp) != NULL) {
		pgref(opgrp);
		lwkt_gettoken(&opgrp->pg_token);
		if (opgrp != p->p_pgrp) {
			lwkt_reltoken(&opgrp->pg_token);
			pgrel(opgrp);
			continue;
		}
		LIST_REMOVE(p, p_pglist);
		break;
	}
	p->p_pgrp = pgrp;
	LIST_INSERT_HEAD(&pgrp->pg_members, p, p_pglist);

	/*
	 * Adjust eligibility of affected pgrps to participate in job control.
	 * Increment eligibility counts before decrementing, otherwise we
	 * could reach 0 spuriously during the first call.
	 */
	fixjobc(p, pgrp, 1);
	if (opgrp) {
		fixjobc(p, opgrp, 0);
		lwkt_reltoken(&opgrp->pg_token);
		pgrel(opgrp);	/* manual pgref */
		pgrel(opgrp);	/* p->p_pgrp ref */
	}
	lwkt_reltoken(&p->p_token);
	lwkt_reltoken(&pgrp->pg_token);
done:
	error = 0;
fatal:
	return (error);
}

/*
 * Remove process from process group
 *
 * No requirements.
 */
int
leavepgrp(struct proc *p)
{
	struct pgrp *pg = p->p_pgrp;

	lwkt_gettoken(&p->p_token);
	while ((pg = p->p_pgrp) != NULL) {
		pgref(pg);
		lwkt_gettoken(&pg->pg_token);
		if (p->p_pgrp != pg) {
			lwkt_reltoken(&pg->pg_token);
			pgrel(pg);
			continue;
		}
		p->p_pgrp = NULL;
		LIST_REMOVE(p, p_pglist);
		lwkt_reltoken(&pg->pg_token);
		pgrel(pg);	/* manual pgref */
		pgrel(pg);	/* p->p_pgrp ref */
		break;
	}
	lwkt_reltoken(&p->p_token);

	return (0);
}

/*
 * Adjust the ref count on a session structure.  When the ref count falls to
 * zero the tty is disassociated from the session and the session structure
 * is freed.  Note that tty assocation is not itself ref-counted.
 *
 * No requirements.
 */
void
sess_hold(struct session *sp)
{
	atomic_add_int(&sp->s_count, 1);
}

/*
 * No requirements.
 */
void
sess_rele(struct session *sess)
{
	struct tty *tp;
	int count;
	int n;

	n = SESS_HASH(sess->s_sid);
	for (;;) {
		count = sess->s_count;
		cpu_ccfence();
		KKASSERT(count > 0);
		if (count == 1) {
			lwkt_gettoken(&tty_token);
			lwkt_gettoken(&proc_tokens[n]);
			if (atomic_cmpset_int(&sess->s_count, 1, 0))
				break;
			lwkt_reltoken(&proc_tokens[n]);
			lwkt_reltoken(&tty_token);
			/* retry */
		} else {
			if (atomic_cmpset_int(&sess->s_count, count, count - 1))
				return;
			/* retry */
		}
	}

	/*
	 * Successful 1->0 transition and tty_token is held.
	 */
	LIST_REMOVE(sess, s_list);
	pid_doms[sess->s_sid % PIDSEL_DOMAINS] = (uint8_t)time_second;

	if (sess->s_ttyp && sess->s_ttyp->t_session) {
#ifdef TTY_DO_FULL_CLOSE
		/* FULL CLOSE, see ttyclearsession() */
		KKASSERT(sess->s_ttyp->t_session == sess);
		sess->s_ttyp->t_session = NULL;
#else
		/* HALF CLOSE, see ttyclearsession() */
		if (sess->s_ttyp->t_session == sess)
			sess->s_ttyp->t_session = NULL;
#endif
	}
	if ((tp = sess->s_ttyp) != NULL) {
		sess->s_ttyp = NULL;
		ttyunhold(tp);
	}
	lwkt_reltoken(&proc_tokens[n]);
	lwkt_reltoken(&tty_token);

	kfree(sess, M_SESSION);
}

/*
 * Adjust pgrp jobc counters when specified process changes process group.
 * We count the number of processes in each process group that "qualify"
 * the group for terminal job control (those with a parent in a different
 * process group of the same session).  If that count reaches zero, the
 * process group becomes orphaned.  Check both the specified process'
 * process group and that of its children.
 * entering == 0 => p is leaving specified group.
 * entering == 1 => p is entering specified group.
 *
 * No requirements.
 */
void
fixjobc(struct proc *p, struct pgrp *pgrp, int entering)
{
	struct pgrp *hispgrp;
	struct session *mysession;
	struct proc *np;

	/*
	 * Check p's parent to see whether p qualifies its own process
	 * group; if so, adjust count for p's process group.
	 */
	lwkt_gettoken(&p->p_token);	/* p_children scan */
	lwkt_gettoken(&pgrp->pg_token);

	mysession = pgrp->pg_session;
	if ((hispgrp = p->p_pptr->p_pgrp) != pgrp &&
	    hispgrp->pg_session == mysession) {
		if (entering)
			pgrp->pg_jobc++;
		else if (--pgrp->pg_jobc == 0)
			orphanpg(pgrp);
	}

	/*
	 * Check this process' children to see whether they qualify
	 * their process groups; if so, adjust counts for children's
	 * process groups.
	 */
	LIST_FOREACH(np, &p->p_children, p_sibling) {
		PHOLD(np);
		lwkt_gettoken(&np->p_token);
		if ((hispgrp = np->p_pgrp) != pgrp &&
		    hispgrp->pg_session == mysession &&
		    np->p_stat != SZOMB) {
			pgref(hispgrp);
			lwkt_gettoken(&hispgrp->pg_token);
			if (entering)
				hispgrp->pg_jobc++;
			else if (--hispgrp->pg_jobc == 0)
				orphanpg(hispgrp);
			lwkt_reltoken(&hispgrp->pg_token);
			pgrel(hispgrp);
		}
		lwkt_reltoken(&np->p_token);
		PRELE(np);
	}
	KKASSERT(pgrp->pg_refs > 0);
	lwkt_reltoken(&pgrp->pg_token);
	lwkt_reltoken(&p->p_token);
}

/*
 * A process group has become orphaned;
 * if there are any stopped processes in the group,
 * hang-up all process in that group.
 *
 * The caller must hold pg_token.
 */
static void
orphanpg(struct pgrp *pg)
{
	struct proc *p;

	LIST_FOREACH(p, &pg->pg_members, p_pglist) {
		if (p->p_stat == SSTOP) {
			LIST_FOREACH(p, &pg->pg_members, p_pglist) {
				ksignal(p, SIGHUP);
				ksignal(p, SIGCONT);
			}
			return;
		}
	}
}

/*
 * Add a new process to the allproc list and the PID hash.  This
 * also assigns a pid to the new process.
 *
 * No requirements.
 */
void
proc_add_allproc(struct proc *p)
{
	int random_offset;

	if ((random_offset = randompid) != 0) {
		read_random(&random_offset, sizeof(random_offset));
		random_offset = (random_offset & 0x7FFFFFFF) % randompid;
	}
	proc_makepid(p, random_offset);
}

/*
 * Calculate a new process pid.  This function is integrated into
 * proc_add_allproc() to guarentee that the new pid is not reused before
 * the new process can be added to the allproc list.
 *
 * p_pid is assigned and the process is added to the allproc hash table
 *
 * WARNING! We need to allocate PIDs sequentially during early boot.
 *	    In particular, init needs to have a pid of 1.
 */
static
void
proc_makepid(struct proc *p, int random_offset)
{
	static pid_t nextpid = 1;	/* heuristic, allowed to race */
	struct pgrp *pg;
	struct proc *ps;
	struct session *sess;
	pid_t base;
	int8_t delta8;
	int retries;
	int n;

	/*
	 * Select the next pid base candidate.
	 *
	 * Check cyclement, do not allow a pid < 100.
	 */
	retries = 0;
retry:
	base = atomic_fetchadd_int(&nextpid, 1) + random_offset;
	if (base <= 0 || base >= PID_MAX) {
		base = base % PID_MAX;
		if (base < 0)
			base = 100;
		if (base < 100)
			base += 100;
		nextpid = base;		/* reset (SMP race ok) */
	}

	/*
	 * Do not allow a base pid to be selected from a domain that has
	 * recently seen a pid/pgid/sessid reap.  Sleep a little if we looped
	 * through all available domains.
	 *
	 * WARNING: We want the early pids to be allocated linearly,
	 *	    particularly pid 1 and pid 2.
	 */
	if (++retries >= PIDSEL_DOMAINS)
		tsleep(&nextpid, 0, "makepid", 1);
	if (base >= 100) {
		delta8 = (int8_t)time_second -
			 (int8_t)pid_doms[base % PIDSEL_DOMAINS];
		if (delta8 >= 0 && delta8 <= PIDDOM_DELAY) {
			++pid_domain_skips;
			goto retry;
		}
	}

	/*
	 * Calculate a hash index and find an unused process id within
	 * the table, looping if we cannot find one.
	 *
	 * The inner loop increments by ALLPROC_HSIZE which keeps the
	 * PID at the same pid_doms[] index as well as the same hash index.
	 */
	n = ALLPROC_HASH(base);
	lwkt_gettoken(&proc_tokens[n]);

restart1:
	LIST_FOREACH(ps, &allprocs[n], p_list) {
		if (ps->p_pid == base) {
			base += ALLPROC_HSIZE;
			if (base >= PID_MAX) {
				lwkt_reltoken(&proc_tokens[n]);
				goto retry;
			}
			++pid_inner_skips;
			goto restart1;
		}
	}
	LIST_FOREACH(pg, &allpgrps[n], pg_list) {
		if (pg->pg_id == base) {
			base += ALLPROC_HSIZE;
			if (base >= PID_MAX) {
				lwkt_reltoken(&proc_tokens[n]);
				goto retry;
			}
			++pid_inner_skips;
			goto restart1;
		}
	}
	LIST_FOREACH(sess, &allsessn[n], s_list) {
		if (sess->s_sid == base) {
			base += ALLPROC_HSIZE;
			if (base >= PID_MAX) {
				lwkt_reltoken(&proc_tokens[n]);
				goto retry;
			}
			++pid_inner_skips;
			goto restart1;
		}
	}

	/*
	 * Assign the pid and insert the process.
	 */
	p->p_pid = base;
	LIST_INSERT_HEAD(&allprocs[n], p, p_list);
	lwkt_reltoken(&proc_tokens[n]);
}

/*
 * Called from exit1 to place the process into a zombie state.
 * The process is removed from the pid hash and p_stat is set
 * to SZOMB.  Normal pfind[n]() calls will not find it any more.
 *
 * Caller must hold p->p_token.  We are required to wait until p_lock
 * becomes zero before we can manipulate the list, allowing allproc
 * scans to guarantee consistency during a list scan.
 */
void
proc_move_allproc_zombie(struct proc *p)
{
	int n;

	n = ALLPROC_HASH(p->p_pid);
	PSTALL(p, "reap1", 0);
	lwkt_gettoken(&proc_tokens[n]);

	PSTALL(p, "reap1a", 0);
	p->p_stat = SZOMB;

	lwkt_reltoken(&proc_tokens[n]);
	dsched_exit_proc(p);
}

/*
 * This routine is called from kern_wait() and will remove the process
 * from the zombie list and the sibling list.  This routine will block
 * if someone has a lock on the proces (p_lock).
 *
 * Caller must hold p->p_token.  We are required to wait until p_lock
 * becomes zero before we can manipulate the list, allowing allproc
 * scans to guarantee consistency during a list scan.
 */
void
proc_remove_zombie(struct proc *p)
{
	int n;

	n = ALLPROC_HASH(p->p_pid);

	PSTALL(p, "reap2", 0);
	lwkt_gettoken(&proc_tokens[n]);
	PSTALL(p, "reap2a", 0);
	LIST_REMOVE(p, p_list);		/* from remove master list */
	LIST_REMOVE(p, p_sibling);	/* and from sibling list */
	p->p_pptr = NULL;
	pid_doms[p->p_pid % PIDSEL_DOMAINS] = (uint8_t)time_second;
	lwkt_reltoken(&proc_tokens[n]);
}

/*
 * Handle various requirements prior to returning to usermode.  Called from
 * platform trap and system call code.
 */
void
lwpuserret(struct lwp *lp)
{
	struct proc *p = lp->lwp_proc;

	if (lp->lwp_mpflags & LWP_MP_VNLRU) {
		atomic_clear_int(&lp->lwp_mpflags, LWP_MP_VNLRU);
		allocvnode_gc();
	}
	if (lp->lwp_mpflags & LWP_MP_WEXIT) {
		lwkt_gettoken(&p->p_token);
		lwp_exit(0, NULL);
		lwkt_reltoken(&p->p_token);     /* NOT REACHED */
	}
}

/*
 * Kernel threads run from user processes can also accumulate deferred
 * actions which need to be acted upon.  Callers include:
 *
 * nfsd		- Can allocate lots of vnodes
 */
void
lwpkthreaddeferred(void)
{
	struct lwp *lp = curthread->td_lwp;

	if (lp) {
		if (lp->lwp_mpflags & LWP_MP_VNLRU) {
			atomic_clear_int(&lp->lwp_mpflags, LWP_MP_VNLRU);
			allocvnode_gc();
		}
	}
}

void
proc_usermap(struct proc *p, int invfork)
{
	struct sys_upmap *upmap;

	lwkt_gettoken(&p->p_token);
	upmap = kmalloc(roundup2(sizeof(*upmap), PAGE_SIZE), M_PROC,
			M_WAITOK | M_ZERO);
	if (p->p_upmap == NULL) {
		upmap->header[0].type = UKPTYPE_VERSION;
		upmap->header[0].offset = offsetof(struct sys_upmap, version);
		upmap->header[1].type = UPTYPE_RUNTICKS;
		upmap->header[1].offset = offsetof(struct sys_upmap, runticks);
		upmap->header[2].type = UPTYPE_FORKID;
		upmap->header[2].offset = offsetof(struct sys_upmap, forkid);
		upmap->header[3].type = UPTYPE_PID;
		upmap->header[3].offset = offsetof(struct sys_upmap, pid);
		upmap->header[4].type = UPTYPE_PROC_TITLE;
		upmap->header[4].offset = offsetof(struct sys_upmap,proc_title);
		upmap->header[5].type = UPTYPE_INVFORK;
		upmap->header[5].offset = offsetof(struct sys_upmap, invfork);

		upmap->version = UPMAP_VERSION;
		upmap->pid = p->p_pid;
		upmap->forkid = p->p_forkid;
		upmap->invfork = invfork;
		p->p_upmap = upmap;
	} else {
		kfree(upmap, M_PROC);
	}
	lwkt_reltoken(&p->p_token);
}

void
proc_userunmap(struct proc *p)
{
	struct sys_upmap *upmap;

	lwkt_gettoken(&p->p_token);
	if ((upmap = p->p_upmap) != NULL) {
		p->p_upmap = NULL;
		kfree(upmap, M_PROC);
	}
	lwkt_reltoken(&p->p_token);
}

/*
 * Scan all processes on the allproc list.  The process is automatically
 * held for the callback.  A return value of -1 terminates the loop.
 * Zombie procs are skipped.
 *
 * The callback is made with the process held and proc_token held.
 *
 * We limit the scan to the number of processes as-of the start of
 * the scan so as not to get caught up in an endless loop if new processes
 * are created more quickly than we can scan the old ones.  Add a little
 * slop to try to catch edge cases since nprocs can race.
 *
 * No requirements.
 */
void
allproc_scan(int (*callback)(struct proc *, void *), void *data)
{
	int limit = nprocs + ncpus;
	struct proc *p;
	int r;
	int n;

	/*
	 * proc_tokens[n] protects the allproc list and PHOLD() prevents the
	 * process from being removed from the allproc list or the zombproc
	 * list.
	 */
	for (n = 0; n < ALLPROC_HSIZE; ++n) {
		if (LIST_FIRST(&allprocs[n]) == NULL)
			continue;
		lwkt_gettoken(&proc_tokens[n]);
		LIST_FOREACH(p, &allprocs[n], p_list) {
			if (p->p_stat == SZOMB)
				continue;
			PHOLD(p);
			r = callback(p, data);
			PRELE(p);
			if (r < 0)
				break;
			if (--limit < 0)
				break;
		}
		lwkt_reltoken(&proc_tokens[n]);

		/*
		 * Check if asked to stop early
		 */
		if (p)
			break;
	}
}

/*
 * Scan all lwps of processes on the allproc list.  The lwp is automatically
 * held for the callback.  A return value of -1 terminates the loop.
 *
 * The callback is made with the proces and lwp both held, and proc_token held.
 *
 * No requirements.
 */
void
alllwp_scan(int (*callback)(struct lwp *, void *), void *data)
{
	struct proc *p;
	struct lwp *lp;
	int r = 0;
	int n;

	for (n = 0; n < ALLPROC_HSIZE; ++n) {
		if (LIST_FIRST(&allprocs[n]) == NULL)
			continue;
		lwkt_gettoken(&proc_tokens[n]);
		LIST_FOREACH(p, &allprocs[n], p_list) {
			if (p->p_stat == SZOMB)
				continue;
			PHOLD(p);
			lwkt_gettoken(&p->p_token);
			FOREACH_LWP_IN_PROC(lp, p) {
				LWPHOLD(lp);
				r = callback(lp, data);
				LWPRELE(lp);
			}
			lwkt_reltoken(&p->p_token);
			PRELE(p);
			if (r < 0)
				break;
		}
		lwkt_reltoken(&proc_tokens[n]);

		/*
		 * Asked to exit early
		 */
		if (p)
			break;
	}
}

/*
 * Scan all processes on the zombproc list.  The process is automatically
 * held for the callback.  A return value of -1 terminates the loop.
 *
 * No requirements.
 * The callback is made with the proces held and proc_token held.
 */
void
zombproc_scan(int (*callback)(struct proc *, void *), void *data)
{
	struct proc *p;
	int r;
	int n;

	/*
	 * proc_tokens[n] protects the allproc list and PHOLD() prevents the
	 * process from being removed from the allproc list or the zombproc
	 * list.
	 */
	for (n = 0; n < ALLPROC_HSIZE; ++n) {
		if (LIST_FIRST(&allprocs[n]) == NULL)
			continue;
		lwkt_gettoken(&proc_tokens[n]);
		LIST_FOREACH(p, &allprocs[n], p_list) {
			if (p->p_stat != SZOMB)
				continue;
			PHOLD(p);
			r = callback(p, data);
			PRELE(p);
			if (r < 0)
				break;
		}
		lwkt_reltoken(&proc_tokens[n]);

		/*
		 * Check if asked to stop early
		 */
		if (p)
			break;
	}
}

#include "opt_ddb.h"
#ifdef DDB
#include <ddb/ddb.h>

/*
 * Debugging only
 */
DB_SHOW_COMMAND(pgrpdump, pgrpdump)
{
	struct pgrp *pgrp;
	struct proc *p;
	int i;

	for (i = 0; i < ALLPROC_HSIZE; ++i) {
		if (LIST_EMPTY(&allpgrps[i]))
			continue;
		kprintf("\tindx %d\n", i);
		LIST_FOREACH(pgrp, &allpgrps[i], pg_list) {
			kprintf("\tpgrp %p, pgid %ld, sess %p, "
				"sesscnt %d, mem %p\n",
				(void *)pgrp, (long)pgrp->pg_id,
				(void *)pgrp->pg_session,
				pgrp->pg_session->s_count,
				(void *)LIST_FIRST(&pgrp->pg_members));
			LIST_FOREACH(p, &pgrp->pg_members, p_pglist) {
				kprintf("\t\tpid %ld addr %p pgrp %p\n",
					(long)p->p_pid, (void *)p,
					(void *)p->p_pgrp);
			}
		}
	}
}
#endif /* DDB */

/*
 * The caller must hold proc_token.
 */
static int
sysctl_out_proc(struct proc *p, struct sysctl_req *req, int flags)
{
	struct kinfo_proc ki;
	struct lwp *lp;
	int skp = 0, had_output = 0;
	int error;

	bzero(&ki, sizeof(ki));
	lwkt_gettoken_shared(&p->p_token);
	fill_kinfo_proc(p, &ki);
	if ((flags & KERN_PROC_FLAG_LWP) == 0)
		skp = 1;
	error = 0;
	FOREACH_LWP_IN_PROC(lp, p) {
		LWPHOLD(lp);
		fill_kinfo_lwp(lp, &ki.kp_lwp);
		had_output = 1;
		error = SYSCTL_OUT(req, &ki, sizeof(ki));
		LWPRELE(lp);
		if (error)
			break;
		if (skp)
			break;
	}
	lwkt_reltoken(&p->p_token);
	/* We need to output at least the proc, even if there is no lwp. */
	if (had_output == 0) {
		error = SYSCTL_OUT(req, &ki, sizeof(ki));
	}
	return (error);
}

/*
 * The caller must hold proc_token.
 */
static int
sysctl_out_proc_kthread(struct thread *td, struct sysctl_req *req)
{
	struct kinfo_proc ki;
	int error;

	fill_kinfo_proc_kthread(td, &ki);
	error = SYSCTL_OUT(req, &ki, sizeof(ki));
	if (error)
		return error;
	return(0);
}

/*
 * No requirements.
 */
static int
sysctl_kern_proc(SYSCTL_HANDLER_ARGS)
{
	int *name = (int *)arg1;
	int oid = oidp->oid_number;
	u_int namelen = arg2;
	struct proc *p;
	struct thread *td;
	struct thread *marker;
	int flags = 0;
	int error = 0;
	int n;
	int origcpu;
	struct ucred *cr1 = curproc->p_ucred;

	flags = oid & KERN_PROC_FLAGMASK;
	oid &= ~KERN_PROC_FLAGMASK;

	if ((oid == KERN_PROC_ALL && namelen != 0) ||
	    (oid != KERN_PROC_ALL && namelen != 1)) {
		return (EINVAL);
	}

	/*
	 * proc_token protects the allproc list and PHOLD() prevents the
	 * process from being removed from the allproc list or the zombproc
	 * list.
	 */
	if (oid == KERN_PROC_PID) {
		p = pfind((pid_t)name[0]);
		if (p) {
			if (PRISON_CHECK(cr1, p->p_ucred))
				error = sysctl_out_proc(p, req, flags);
			PRELE(p);
		}
		goto post_threads;
	}
	p = NULL;

	if (!req->oldptr) {
		/* overestimate by 5 procs */
		error = SYSCTL_OUT(req, 0, sizeof (struct kinfo_proc) * 5);
		if (error)
			goto post_threads;
	}

	for (n = 0; n < ALLPROC_HSIZE; ++n) {
		if (LIST_EMPTY(&allprocs[n]))
			continue;
		lwkt_gettoken_shared(&proc_tokens[n]);
		LIST_FOREACH(p, &allprocs[n], p_list) {
			/*
			 * Show a user only their processes.
			 */
			if ((!ps_showallprocs) &&
				(p->p_ucred == NULL || p_trespass(cr1, p->p_ucred))) {
				continue;
			}
			/*
			 * Skip embryonic processes.
			 */
			if (p->p_stat == SIDL)
				continue;
			/*
			 * TODO - make more efficient (see notes below).
			 * do by session.
			 */
			switch (oid) {
			case KERN_PROC_PGRP:
				/* could do this by traversing pgrp */
				if (p->p_pgrp == NULL || 
				    p->p_pgrp->pg_id != (pid_t)name[0])
					continue;
				break;

			case KERN_PROC_TTY:
				if ((p->p_flags & P_CONTROLT) == 0 ||
				    p->p_session == NULL ||
				    p->p_session->s_ttyp == NULL ||
				    dev2udev(p->p_session->s_ttyp->t_dev) != 
					(udev_t)name[0])
					continue;
				break;

			case KERN_PROC_UID:
				if (p->p_ucred == NULL || 
				    p->p_ucred->cr_uid != (uid_t)name[0])
					continue;
				break;

			case KERN_PROC_RUID:
				if (p->p_ucred == NULL || 
				    p->p_ucred->cr_ruid != (uid_t)name[0])
					continue;
				break;
			}

			if (!PRISON_CHECK(cr1, p->p_ucred))
				continue;
			PHOLD(p);
			error = sysctl_out_proc(p, req, flags);
			PRELE(p);
			if (error) {
				lwkt_reltoken(&proc_tokens[n]);
				goto post_threads;
			}
		}
		lwkt_reltoken(&proc_tokens[n]);
	}

	/*
	 * Iterate over all active cpus and scan their thread list.  Start
	 * with the next logical cpu and end with our original cpu.  We
	 * migrate our own thread to each target cpu in order to safely scan
	 * its thread list.  In the last loop we migrate back to our original
	 * cpu.
	 */
	origcpu = mycpu->gd_cpuid;
	if (!ps_showallthreads || jailed(cr1))
		goto post_threads;

	marker = kmalloc(sizeof(struct thread), M_TEMP, M_WAITOK|M_ZERO);
	marker->td_flags = TDF_MARKER;
	error = 0;

	for (n = 1; n <= ncpus; ++n) {
		globaldata_t rgd;
		int nid;

		nid = (origcpu + n) % ncpus;
		if (CPUMASK_TESTBIT(smp_active_mask, nid) == 0)
			continue;
		rgd = globaldata_find(nid);
		lwkt_setcpu_self(rgd);

		crit_enter();
		TAILQ_INSERT_TAIL(&rgd->gd_tdallq, marker, td_allq);

		while ((td = TAILQ_PREV(marker, lwkt_queue, td_allq)) != NULL) {
			TAILQ_REMOVE(&rgd->gd_tdallq, marker, td_allq);
			TAILQ_INSERT_BEFORE(td, marker, td_allq);
			if (td->td_flags & TDF_MARKER)
				continue;
			if (td->td_proc)
				continue;

			lwkt_hold(td);
			crit_exit();

			switch (oid) {
			case KERN_PROC_PGRP:
			case KERN_PROC_TTY:
			case KERN_PROC_UID:
			case KERN_PROC_RUID:
				break;
			default:
				error = sysctl_out_proc_kthread(td, req);
				break;
			}
			lwkt_rele(td);
			crit_enter();
			if (error)
				break;
		}
		TAILQ_REMOVE(&rgd->gd_tdallq, marker, td_allq);
		crit_exit();

		if (error)
			break;
	}

	/*
	 * Userland scheduler expects us to return on the same cpu we
	 * started on.
	 */
	if (mycpu->gd_cpuid != origcpu)
		lwkt_setcpu_self(globaldata_find(origcpu));

	kfree(marker, M_TEMP);

post_threads:
	return (error);
}

/*
 * This sysctl allows a process to retrieve the argument list or process
 * title for another process without groping around in the address space
 * of the other process.  It also allow a process to set its own "process 
 * title to a string of its own choice.
 *
 * No requirements.
 */
static int
sysctl_kern_proc_args(SYSCTL_HANDLER_ARGS)
{
	int *name = (int*) arg1;
	u_int namelen = arg2;
	struct proc *p;
	struct pargs *opa;
	struct pargs *pa;
	int error = 0;
	struct ucred *cr1 = curproc->p_ucred;

	if (namelen != 1) 
		return (EINVAL);

	p = pfind((pid_t)name[0]);
	if (p == NULL)
		goto done;
	lwkt_gettoken(&p->p_token);

	if ((!ps_argsopen) && p_trespass(cr1, p->p_ucred))
		goto done;

	if (req->newptr && curproc != p) {
		error = EPERM;
		goto done;
	}
	if (req->oldptr) {
		if (p->p_upmap != NULL && p->p_upmap->proc_title[0]) {
			/*
			 * Args set via writable user process mmap.
			 * We must calculate the string length manually
			 * because the user data can change at any time.
			 */
			size_t n;
			char *base;

			base = p->p_upmap->proc_title;
			for (n = 0; n < UPMAP_MAXPROCTITLE - 1; ++n) {
				if (base[n] == 0)
					break;
			}
			error = SYSCTL_OUT(req, base, n);
			if (error == 0)
				error = SYSCTL_OUT(req, "", 1);
		} else if ((pa = p->p_args) != NULL) {
			/*
			 * Args set by setproctitle() sysctl.
			 */
			refcount_acquire(&pa->ar_ref);
			error = SYSCTL_OUT(req, pa->ar_args, pa->ar_length);
			if (refcount_release(&pa->ar_ref))
				kfree(pa, M_PARGS);
		}
	}
	if (req->newptr == NULL)
		goto done;

	if (req->newlen + sizeof(struct pargs) > ps_arg_cache_limit) {
		goto done;
	}

	pa = kmalloc(sizeof(struct pargs) + req->newlen, M_PARGS, M_WAITOK);
	refcount_init(&pa->ar_ref, 1);
	pa->ar_length = req->newlen;
	error = SYSCTL_IN(req, pa->ar_args, req->newlen);
	if (error) {
		kfree(pa, M_PARGS);
		goto done;
	}


	/*
	 * Replace p_args with the new pa.  p_args may have previously
	 * been NULL.
	 */
	opa = p->p_args;
	p->p_args = pa;

	if (opa) {
		KKASSERT(opa->ar_ref > 0);
		if (refcount_release(&opa->ar_ref)) {
			kfree(opa, M_PARGS);
			/* opa = NULL; */
		}
	}
done:
	if (p) {
		lwkt_reltoken(&p->p_token);
		PRELE(p);
	}
	return (error);
}

static int
sysctl_kern_proc_cwd(SYSCTL_HANDLER_ARGS)
{
	int *name = (int*) arg1;
	u_int namelen = arg2;
	struct proc *p;
	int error = 0;
	char *fullpath, *freepath;
	struct ucred *cr1 = curproc->p_ucred;

	if (namelen != 1) 
		return (EINVAL);

	p = pfind((pid_t)name[0]);
	if (p == NULL)
		goto done;
	lwkt_gettoken_shared(&p->p_token);

	/*
	 * If we are not allowed to see other args, we certainly shouldn't
	 * get the cwd either. Also check the usual trespassing.
	 */
	if ((!ps_argsopen) && p_trespass(cr1, p->p_ucred))
		goto done;

	if (req->oldptr && p->p_fd != NULL && p->p_fd->fd_ncdir.ncp) {
		struct nchandle nch;

		cache_copy(&p->p_fd->fd_ncdir, &nch);
		error = cache_fullpath(p, &nch, NULL,
				       &fullpath, &freepath, 0);
		cache_drop(&nch);
		if (error)
			goto done;
		error = SYSCTL_OUT(req, fullpath, strlen(fullpath) + 1);
		kfree(freepath, M_TEMP);
	}

done:
	if (p) {
		lwkt_reltoken(&p->p_token);
		PRELE(p);
	}
	return (error);
}

/*
 * This sysctl allows a process to retrieve the path of the executable for
 * itself or another process.
 */
static int
sysctl_kern_proc_pathname(SYSCTL_HANDLER_ARGS)
{
	pid_t *pidp = (pid_t *)arg1;
	unsigned int arglen = arg2;
	struct proc *p;
	struct vnode *vp;
	char *retbuf, *freebuf;
	int error = 0;

	if (arglen != 1)
		return (EINVAL);
	if (*pidp == -1) {	/* -1 means this process */
		p = curproc;
	} else {
		p = pfind(*pidp);
		if (p == NULL)
			return (ESRCH);
	}

	vp = p->p_textvp;
	if (vp == NULL)
		goto done;

	vref(vp);
	error = vn_fullpath(p, vp, &retbuf, &freebuf, 0);
	vrele(vp);
	if (error)
		goto done;
	error = SYSCTL_OUT(req, retbuf, strlen(retbuf) + 1);
	kfree(freebuf, M_TEMP);
done:
	if(*pidp != -1)
		PRELE(p);

	return (error);
}

SYSCTL_NODE(_kern, KERN_PROC, proc, CTLFLAG_RD,  0, "Process table");

SYSCTL_PROC(_kern_proc, KERN_PROC_ALL, all, CTLFLAG_RD|CTLTYPE_STRUCT,
	0, 0, sysctl_kern_proc, "S,proc", "Return entire process table");

SYSCTL_NODE(_kern_proc, KERN_PROC_PGRP, pgrp, CTLFLAG_RD, 
	sysctl_kern_proc, "Process table");

SYSCTL_NODE(_kern_proc, KERN_PROC_TTY, tty, CTLFLAG_RD, 
	sysctl_kern_proc, "Process table");

SYSCTL_NODE(_kern_proc, KERN_PROC_UID, uid, CTLFLAG_RD, 
	sysctl_kern_proc, "Process table");

SYSCTL_NODE(_kern_proc, KERN_PROC_RUID, ruid, CTLFLAG_RD, 
	sysctl_kern_proc, "Process table");

SYSCTL_NODE(_kern_proc, KERN_PROC_PID, pid, CTLFLAG_RD, 
	sysctl_kern_proc, "Process table");

SYSCTL_NODE(_kern_proc, (KERN_PROC_ALL | KERN_PROC_FLAG_LWP), all_lwp, CTLFLAG_RD,
	sysctl_kern_proc, "Process table");

SYSCTL_NODE(_kern_proc, (KERN_PROC_PGRP | KERN_PROC_FLAG_LWP), pgrp_lwp, CTLFLAG_RD, 
	sysctl_kern_proc, "Process table");

SYSCTL_NODE(_kern_proc, (KERN_PROC_TTY | KERN_PROC_FLAG_LWP), tty_lwp, CTLFLAG_RD, 
	sysctl_kern_proc, "Process table");

SYSCTL_NODE(_kern_proc, (KERN_PROC_UID | KERN_PROC_FLAG_LWP), uid_lwp, CTLFLAG_RD, 
	sysctl_kern_proc, "Process table");

SYSCTL_NODE(_kern_proc, (KERN_PROC_RUID | KERN_PROC_FLAG_LWP), ruid_lwp, CTLFLAG_RD, 
	sysctl_kern_proc, "Process table");

SYSCTL_NODE(_kern_proc, (KERN_PROC_PID | KERN_PROC_FLAG_LWP), pid_lwp, CTLFLAG_RD, 
	sysctl_kern_proc, "Process table");

SYSCTL_NODE(_kern_proc, KERN_PROC_ARGS, args, CTLFLAG_RW | CTLFLAG_ANYBODY,
	sysctl_kern_proc_args, "Process argument list");

SYSCTL_NODE(_kern_proc, KERN_PROC_CWD, cwd, CTLFLAG_RD | CTLFLAG_ANYBODY,
	sysctl_kern_proc_cwd, "Process argument list");

static SYSCTL_NODE(_kern_proc, KERN_PROC_PATHNAME, pathname, CTLFLAG_RD,
	sysctl_kern_proc_pathname, "Process executable path");
