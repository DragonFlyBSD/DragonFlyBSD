/*	$NetBSD: linux_futex.c,v 1.7 2006/07/24 19:01:49 manu Exp $ */

/*-
 * Copyright (c) 2005 Emmanuel Dreyfus, all rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. All advertising materials mentioning features or use of this software
 *    must display the following acknowledgement:
 *	This product includes software developed by Emmanuel Dreyfus
 * 4. The name of the author may not be used to endorse or promote
 *    products derived from this software without specific prior written
 *    permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE THE AUTHOR AND CONTRIBUTORS ``AS IS''
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO,
 * THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS
 * BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

#include "opt_compat.h"

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/imgact.h>
#include <sys/imgact_aout.h>
#include <sys/imgact_elf.h>
#include <sys/kern_syscall.h>
#include <sys/lock.h>
#include <sys/malloc.h>
#include <sys/proc.h>
#include <sys/signalvar.h>
#include <sys/sysent.h>
#include <sys/sysproto.h>
#include <sys/priv.h>
#include <sys/lock.h>
#include <sys/spinlock2.h>

#include <vm/vm.h>
#include <vm/vm_param.h>
#include <vm/vm_page.h>
#include <vm/vm_extern.h>
#include <sys/exec.h>
#include <sys/kernel.h>
#include <sys/module.h>
#include <machine/cpu.h>
#include <machine/limits.h>

#include "i386/linux.h"
#include "i386/linux_proto.h"
#include "linux_signal.h"
#include "linux_util.h"
#include "linux_emuldata.h"

MALLOC_DEFINE(M_FUTEX, "futex", "Linux futexes");
MALLOC_DEFINE(M_FUTEX_WP, "futex wp", "Linux futexes wp");

struct futex;

struct waiting_proc {
	uint32_t	wp_flags;
	struct futex	*wp_futex;
	TAILQ_ENTRY(waiting_proc) wp_list;
};

struct futex {
	struct lock	f_lck;
	uint32_t	*f_uaddr;
	uint32_t	f_refcount;
	LIST_ENTRY(futex) f_list;
	TAILQ_HEAD(lf_waiting_proc, waiting_proc) f_waiting_proc;
};

struct futex_list futex_list;

#if 0
#define FUTEX_LOCK(f)		spin_lock(&(f)->f_lck)
#define FUTEX_UNLOCK(f)		spin_unlock(&(f)->f_lck)
#define FUTEX_INIT(f)		spin_init(&(f)->f_lck, "futex")
#define	FUTEX_SLEEP(f, id, flag, wm, timo)	ssleep((id), &(f)->f_lck, (flag), (wm), (timo))
#endif

#define FUTEX_LOCK(f)		lockmgr(&(f)->f_lck, LK_EXCLUSIVE)
#define FUTEX_UNLOCK(f)		lockmgr(&(f)->f_lck, LK_RELEASE)
#define FUTEX_INIT(f)		lockinit(&(f)->f_lck, "ftlk", 0, LK_CANRECURSE)
#define FUTEX_DESTROY(f)	lockuninit(&(f)->f_lck)
#define FUTEX_ASSERT_LOCKED(f)	KKASSERT(lockstatus(&(f)->f_lck, curthread) == LK_EXCLUSIVE)
#define	FUTEX_SLEEP(f, id, flag, wm, timo)	lksleep((id), &(f)->f_lck, (flag), (wm), (timo))

struct lock futex_mtx;			/* protects the futex list */
#define FUTEXES_LOCK		lockmgr(&futex_mtx, LK_EXCLUSIVE)
#define FUTEXES_UNLOCK		lockmgr(&futex_mtx, LK_RELEASE)

/* Debug magic to take advantage of freebsd's mess */
#ifdef LINUX_DEBUG
#define LINUX_CTR_PREFIX
#else
#define LINUX_CTR_PREFIX	while (0)
#endif

#define LINUX_CTR1(a,b,c)	LINUX_CTR_PREFIX kprintf("linux_futex: " b "\n",c)
#define LINUX_CTR2(a,b,c,d)	LINUX_CTR_PREFIX kprintf("linux_futex: " b "\n",c,d)
#define LINUX_CTR3(a,b,c,d,e)	LINUX_CTR_PREFIX kprintf("linux_futex: " b "\n",c,d,e)
#define LINUX_CTR4(a,b,c,d,e,f)	LINUX_CTR_PREFIX kprintf("linux_futex: " b "\n",c,d,e,f)
#define LINUX_CTR5(a,b,c,d,e,f,g)	LINUX_CTR_PREFIX kprintf("linux_futex: " b "\n",c,d,e,f,g)



/* flags for futex_get() */
#define FUTEX_CREATE_WP		0x1	/* create waiting_proc */
#define FUTEX_DONTCREATE	0x2	/* don't create futex if not exists */
#define FUTEX_DONTEXISTS	0x4	/* return EINVAL if futex exists */

/* wp_flags */
#define FUTEX_WP_REQUEUED	0x1	/* wp requeued - wp moved from wp_list
					 * of futex where thread sleep to wp_list
					 * of another futex.
					 */
#define FUTEX_WP_REMOVED	0x2	/* wp is woken up and removed from futex
					 * wp_list to prevent double wakeup.
					 */

/* support.s */
int futex_xchgl(int oparg, uint32_t *uaddr, int *oldval);
int futex_addl(int oparg, uint32_t *uaddr, int *oldval);
int futex_orl(int oparg, uint32_t *uaddr, int *oldval);
int futex_andl(int oparg, uint32_t *uaddr, int *oldval);
int futex_xorl(int oparg, uint32_t *uaddr, int *oldval);

static void
futex_put(struct futex *f, struct waiting_proc *wp)
{
	FUTEX_ASSERT_LOCKED(f);
	if (wp != NULL) {
		if ((wp->wp_flags & FUTEX_WP_REMOVED) == 0)
			TAILQ_REMOVE(&f->f_waiting_proc, wp, wp_list);
		kfree(wp, M_FUTEX_WP);
	}

	FUTEXES_LOCK;
	if (--f->f_refcount == 0) {
		LIST_REMOVE(f, f_list);
		FUTEXES_UNLOCK;
		FUTEX_UNLOCK(f);

		LINUX_CTR2(sys_futex, "futex_put destroy uaddr %p ref %d",
		    f->f_uaddr, f->f_refcount);
		FUTEX_DESTROY(f);
		kfree(f, M_FUTEX);
		return;
	}

	LINUX_CTR2(sys_futex, "futex_put uaddr %p ref %d",
	    f->f_uaddr, f->f_refcount);
	FUTEXES_UNLOCK;
	FUTEX_UNLOCK(f);
}

static int
futex_get0(uint32_t *uaddr, struct futex **newf, uint32_t flags)
{
	struct futex *f, *tmpf;

	*newf = tmpf = NULL;

retry:
	FUTEXES_LOCK;
	LIST_FOREACH(f, &futex_list, f_list) {
		if (f->f_uaddr == uaddr) {
			if (tmpf != NULL) {
				FUTEX_UNLOCK(tmpf);
				FUTEX_DESTROY(tmpf);
				kfree(tmpf, M_FUTEX);
			}
			if (flags & FUTEX_DONTEXISTS) {
				FUTEXES_UNLOCK;
				return (EINVAL);
			}

			/*
			 * Increment refcount of the found futex to
			 * prevent it from deallocation before FUTEX_LOCK()
			 */
			++f->f_refcount;
			FUTEXES_UNLOCK;

			FUTEX_LOCK(f);
			*newf = f;
			LINUX_CTR2(sys_futex, "futex_get uaddr %p ref %d",
			    uaddr, f->f_refcount);
			return (0);
		}
	}

	if (flags & FUTEX_DONTCREATE) {
		FUTEXES_UNLOCK;
		LINUX_CTR1(sys_futex, "futex_get uaddr %p null", uaddr);
		return (0);
	}

	if (tmpf == NULL) {
		FUTEXES_UNLOCK;
		tmpf = kmalloc(sizeof(*tmpf), M_FUTEX, M_WAITOK | M_ZERO);
		tmpf->f_uaddr = uaddr;
		tmpf->f_refcount = 1;
		FUTEX_INIT(tmpf);
		TAILQ_INIT(&tmpf->f_waiting_proc);

		/*
		 * Lock the new futex before an insert into the futex_list
		 * to prevent futex usage by other.
		 */
		FUTEX_LOCK(tmpf);
		goto retry;
	}

	LIST_INSERT_HEAD(&futex_list, tmpf, f_list);
	FUTEXES_UNLOCK;

	LINUX_CTR2(sys_futex, "futex_get uaddr %p ref %d new",
	    uaddr, tmpf->f_refcount);
	*newf = tmpf;
	return (0);
}

static int
futex_get(uint32_t *uaddr, struct waiting_proc **wp, struct futex **f,
    uint32_t flags)
{
	int error;

	if (flags & FUTEX_CREATE_WP) {
		*wp = kmalloc(sizeof(struct waiting_proc), M_FUTEX_WP, M_WAITOK);
		(*wp)->wp_flags = 0;
	}
	error = futex_get0(uaddr, f, flags);
	if (error) {
		if (flags & FUTEX_CREATE_WP)
			kfree(*wp, M_FUTEX_WP);
		return (error);
	}
	if (flags & FUTEX_CREATE_WP) {
		TAILQ_INSERT_HEAD(&(*f)->f_waiting_proc, *wp, wp_list);
		(*wp)->wp_futex = *f;
	}

	return (error);
}

static int
futex_sleep(struct futex *f, struct waiting_proc *wp, int timeout)
{
	int error;

	FUTEX_ASSERT_LOCKED(f);
	LINUX_CTR4(sys_futex, "futex_sleep enter uaddr %p wp %p timo %d ref %d",
	    f->f_uaddr, wp, timeout, f->f_refcount);
	error = FUTEX_SLEEP(f, wp, PCATCH, "futex", timeout);
	if (wp->wp_flags & FUTEX_WP_REQUEUED) {
		KASSERT(f != wp->wp_futex, ("futex != wp_futex"));
		LINUX_CTR5(sys_futex, "futex_sleep out error %d uaddr %p w"
		    " %p requeued uaddr %p ref %d",
		    error, f->f_uaddr, wp, wp->wp_futex->f_uaddr,
		    wp->wp_futex->f_refcount);
		futex_put(f, NULL);
		f = wp->wp_futex;
		FUTEX_LOCK(f);
	}

	futex_put(f, wp);
	return (error);
}

static int
futex_wake(struct futex *f, int n)
{
	struct waiting_proc *wp, *wpt;
	int count = 0;

	FUTEX_ASSERT_LOCKED(f);
	TAILQ_FOREACH_MUTABLE(wp, &f->f_waiting_proc, wp_list, wpt) {
		LINUX_CTR3(sys_futex, "futex_wake uaddr %p wp %p ref %d",
		    f->f_uaddr, wp, f->f_refcount);
		wp->wp_flags |= FUTEX_WP_REMOVED;
		TAILQ_REMOVE(&f->f_waiting_proc, wp, wp_list);
		wakeup_one(wp);
		if (++count == n)
			break;
	}

	return (count);
}

static int
futex_requeue(struct futex *f, int n, struct futex *f2, int n2)
{
	struct waiting_proc *wp, *wpt;
	int count = 0;

	FUTEX_ASSERT_LOCKED(f);
	FUTEX_ASSERT_LOCKED(f2);

	TAILQ_FOREACH_MUTABLE(wp, &f->f_waiting_proc, wp_list, wpt) {
		if (++count <= n) {
			LINUX_CTR2(sys_futex, "futex_req_wake uaddr %p wp %p",
			    f->f_uaddr, wp);
			wp->wp_flags |= FUTEX_WP_REMOVED;
			TAILQ_REMOVE(&f->f_waiting_proc, wp, wp_list);
			wakeup_one(wp);
		} else {
			LINUX_CTR3(sys_futex, "futex_requeue uaddr %p wp %p to %p",
			    f->f_uaddr, wp, f2->f_uaddr);
			wp->wp_flags |= FUTEX_WP_REQUEUED;
			/* Move wp to wp_list of f2 futex */
			TAILQ_REMOVE(&f->f_waiting_proc, wp, wp_list);
			TAILQ_INSERT_HEAD(&f2->f_waiting_proc, wp, wp_list);

			/*
			 * Thread which sleeps on wp after waking should
			 * acquire f2 lock, so increment refcount of f2 to
			 * prevent it from premature deallocation.
			 */
			wp->wp_futex = f2;
			FUTEXES_LOCK;
			++f2->f_refcount;
			FUTEXES_UNLOCK;
			if (count - n >= n2)
				break;
		}
	}

	return (count);
}

static int
futex_wait(struct futex *f, struct waiting_proc *wp, struct l_timespec *ts)
{
	struct l_timespec timeout;
	struct timeval tv;
	int timeout_hz;
	int error;

	if (ts != NULL) {
		error = copyin(ts, &timeout, sizeof(timeout));
		if (error)
			return (error);
		TIMESPEC_TO_TIMEVAL(&tv, &timeout);
		error = itimerfix(&tv);
		if (error)
			return (error);
		timeout_hz = tvtohz_high(&tv);
	} else {	
		timeout_hz = 0;
	}

	error = futex_sleep(f, wp, timeout_hz);
	if (error == EWOULDBLOCK)
		error = ETIMEDOUT;

	return (error);
}

static int
futex_atomic_op(struct proc *p, int encoded_op, uint32_t *uaddr)
{
	int op = (encoded_op >> 28) & 7;
	int cmp = (encoded_op >> 24) & 15;
	int oparg = (encoded_op << 8) >> 20;
	int cmparg = (encoded_op << 20) >> 20;
	int oldval = 0, ret;

	if (encoded_op & (FUTEX_OP_OPARG_SHIFT << 28))
		oparg = 1 << oparg;

#ifdef DEBUG
	if (ldebug(sys_futex))
		kprintf("futex_atomic_op: op = %d, cmp = %d, oparg = %x, "
		       "cmparg = %x, uaddr = %p\n",
		       op, cmp, oparg, cmparg, uaddr);
#endif
	/* XXX: linux verifies access here and returns EFAULT */

	switch (op) {
	case FUTEX_OP_SET:
		ret = futex_xchgl(oparg, uaddr, &oldval);
		break;
	case FUTEX_OP_ADD:
		ret = futex_addl(oparg, uaddr, &oldval);
		break;
	case FUTEX_OP_OR:
		ret = futex_orl(oparg, uaddr, &oldval);
		break;
	case FUTEX_OP_ANDN:
		ret = futex_andl(~oparg, uaddr, &oldval);
		break;
	case FUTEX_OP_XOR:
		ret = futex_xorl(oparg, uaddr, &oldval);
		break;
	default:
		ret = -ENOSYS;
		break;
	}

	if (ret)
		return (ret);

	switch (cmp) {
	case FUTEX_OP_CMP_EQ:
		return (oldval == cmparg);
	case FUTEX_OP_CMP_NE:
		return (oldval != cmparg);
	case FUTEX_OP_CMP_LT:
		return (oldval < cmparg);
	case FUTEX_OP_CMP_GE:
		return (oldval >= cmparg);
	case FUTEX_OP_CMP_LE:
		return (oldval <= cmparg);
	case FUTEX_OP_CMP_GT:
		return (oldval > cmparg);
	default:
		return (-ENOSYS);
	}
}

int
sys_linux_sys_futex(struct linux_sys_futex_args *args)
{
	int op_ret, val, ret, nrwake;
	struct waiting_proc *wp;
	struct futex *f, *f2 = NULL;
	int error = 0;

	/*
	 * Our implementation provides only privates futexes. Most of the apps
	 * should use private futexes but don't claim so. Therefore we treat
	 * all futexes as private by clearing the FUTEX_PRIVATE_FLAG. It works
	 * in most cases (ie. when futexes are not shared on file descriptor
	 * or between different processes.).
	 */
	args->op = (args->op & ~LINUX_FUTEX_PRIVATE_FLAG);

	switch (args->op) {
	case LINUX_FUTEX_WAIT:
		LINUX_CTR2(sys_futex, "WAIT val %d uaddr %p",
		    args->val, args->uaddr);
#ifdef DEBUG
		if (ldebug(sys_futex))
			kprintf(ARGS(sys_futex, "futex_wait val %d uaddr %p"),
			    args->val, args->uaddr);
#endif
		error = futex_get(args->uaddr, &wp, &f, FUTEX_CREATE_WP);
		if (error)
			return (error);
		error = copyin(args->uaddr, &val, sizeof(val));
		if (error) {
			LINUX_CTR1(sys_futex, "WAIT copyin failed %d",
			    error);
			futex_put(f, wp);
			return (error);
		}
		if (val != args->val) {
			LINUX_CTR3(sys_futex, "WAIT uaddr %p val %d != uval %d",
			    args->uaddr, args->val, val);
			futex_put(f, wp);
			return (EWOULDBLOCK);
		}

		error = futex_wait(f, wp, args->timeout);
		break;

	case LINUX_FUTEX_WAKE:

		LINUX_CTR2(sys_futex, "WAKE val %d uaddr %p",
		    args->val, args->uaddr);

		/*
		 * XXX: Linux is able to cope with different addresses
		 * corresponding to the same mapped memory in the sleeping
		 * and waker process(es).
		 */
#ifdef DEBUG
		if (ldebug(sys_futex))
			kprintf(ARGS(sys_futex, "futex_wake val %d uaddr %p"),
			    args->val, args->uaddr);
#endif
		error = futex_get(args->uaddr, NULL, &f, FUTEX_DONTCREATE);
		if (error)
			return (error);
		if (f == NULL) {
			args->sysmsg_iresult = 0;
			return (error);
		}
		args->sysmsg_iresult = futex_wake(f, args->val);
		futex_put(f, NULL);
		break;

	case LINUX_FUTEX_CMP_REQUEUE:

		LINUX_CTR5(sys_futex, "CMP_REQUEUE uaddr %p "
		    "val %d val3 %d uaddr2 %p val2 %d",
		    args->uaddr, args->val, args->val3, args->uaddr2,
		    (int)(unsigned long)args->timeout);

#ifdef DEBUG
		if (ldebug(sys_futex))
			kprintf(ARGS(sys_futex, "futex_cmp_requeue uaddr %p "
			    "val %d val3 %d uaddr2 %p val2 %d"),
			    args->uaddr, args->val, args->val3, args->uaddr2,
			    (int)(unsigned long)args->timeout);
#endif
		/*
		 * Linux allows this, we would not, it is an incorrect
		 * usage of declared ABI, so return EINVAL.
		 */
		if (args->uaddr == args->uaddr2)
			return (EINVAL);
		error = futex_get0(args->uaddr, &f, 0);
		if (error)
			return (error);

		/*
		 * To avoid deadlocks return EINVAL if second futex
		 * exists at this time. Otherwise create the new futex
		 * and ignore false positive LOR which thus happens.
		 *
		 * Glibc fall back to FUTEX_WAKE in case of any error
		 * returned by FUTEX_CMP_REQUEUE.
		 */
		error = futex_get0(args->uaddr2, &f2, FUTEX_DONTEXISTS);
		if (error) {
			futex_put(f, NULL);
			return (error);
		}
		error = copyin(args->uaddr, &val, sizeof(val));
		if (error) {
			LINUX_CTR1(sys_futex, "CMP_REQUEUE copyin failed %d",
			    error);
			futex_put(f2, NULL);
			futex_put(f, NULL);
			return (error);
		}
		if (val != args->val3) {
			LINUX_CTR2(sys_futex, "CMP_REQUEUE val %d != uval %d",
			    args->val, val);
			futex_put(f2, NULL);
			futex_put(f, NULL);
			return (EAGAIN);
		}

		nrwake = (int)(unsigned long)args->timeout;
		args->sysmsg_iresult = futex_requeue(f, args->val, f2, nrwake);
		futex_put(f2, NULL);
		futex_put(f, NULL);
		break;

	case LINUX_FUTEX_WAKE_OP:

		LINUX_CTR5(sys_futex, "WAKE_OP "
		    "uaddr %p op %d val %x uaddr2 %p val3 %x",
		    args->uaddr, args->op, args->val,
		    args->uaddr2, args->val3);

#ifdef DEBUG
		if (ldebug(sys_futex))
			kprintf(ARGS(sys_futex, "futex_wake_op "
			    "uaddr %p op %d val %x uaddr2 %p val3 %x"),
			    args->uaddr, args->op, args->val,
			    args->uaddr2, args->val3);
#endif
		error = futex_get0(args->uaddr, &f, 0);
		if (error)
			return (error);
		if (args->uaddr != args->uaddr2)
			error = futex_get0(args->uaddr2, &f2, 0);
		if (error) {
			futex_put(f, NULL);
			return (error);
		}

		/*
		 * This function returns positive number as results and
		 * negative as errors
		 */
		op_ret = futex_atomic_op(curproc, args->val3, args->uaddr2);

		if (op_ret < 0) {
			/* XXX: We don't handle the EFAULT yet. */
			if (op_ret != -EFAULT) {
				if (f2 != NULL)
					futex_put(f2, NULL);
				futex_put(f, NULL);
				return (-op_ret);
			}
			if (f2 != NULL)
				futex_put(f2, NULL);
			futex_put(f, NULL);
			return (EFAULT);
		}

		ret = futex_wake(f, args->val);

		if (op_ret > 0) {
			op_ret = 0;
			nrwake = (int)(unsigned long)args->timeout;

			if (f2 != NULL)
				op_ret += futex_wake(f2, nrwake);
			else
				op_ret += futex_wake(f, nrwake);
			ret += op_ret;

		}
		if (f2 != NULL)
			futex_put(f2, NULL);
		futex_put(f, NULL);
		args->sysmsg_iresult = ret;
		break;

	case LINUX_FUTEX_LOCK_PI:
		/* not yet implemented */
		return (ENOSYS);

	case LINUX_FUTEX_UNLOCK_PI:
		/* not yet implemented */
		return (ENOSYS);

	case LINUX_FUTEX_TRYLOCK_PI:
		/* not yet implemented */
		return (ENOSYS);

	case LINUX_FUTEX_REQUEUE:

		/*
		 * Glibc does not use this operation since version 2.3.3,
		 * as it is racy and replaced by FUTEX_CMP_REQUEUE operation.
		 * Glibc versions prior to 2.3.3 fall back to FUTEX_WAKE when
		 * FUTEX_REQUEUE returned EINVAL.
		 */
		return (EINVAL);

	default:
		kprintf("linux_sys_futex: unknown op %d\n", args->op);
		return (ENOSYS);
	}

	return (error);
}

int
sys_linux_set_robust_list(struct linux_set_robust_list_args *args)
{
#ifdef DEBUG
	if (ldebug(set_robust_list))
		kprintf(ARGS(set_robust_list, "head %p len %d"),
		    args->head, args->len);
#endif

	if (args->len != sizeof(struct linux_robust_list_head))
		return (EINVAL);

	emuldata_set_robust(curproc, args->head);

	return (0);
}



int
sys_linux_get_robust_list(struct linux_get_robust_list_args *args)
{
	struct linux_emuldata *em;
	struct linux_robust_list_head empty_head;
	struct linux_robust_list_head *head;
	l_size_t len = sizeof(struct linux_robust_list_head);
	int error = 0;

#ifdef	DEBUG
	if (ldebug(get_robust_list))
		kprintf(ARGS(get_robust_list, ""));
#endif

	if (args->pid == 0) {
		EMUL_LOCK();
		em = emuldata_get(curproc);
		KKASSERT(em != NULL);
		if (em->robust_futexes == NULL) {
			bzero(&empty_head, sizeof(empty_head));
			head = &empty_head;
		} else {
			head = em->robust_futexes;
		}
		EMUL_UNLOCK();
	} else {
		struct proc *p;

		p = pfind(args->pid);
		if (p == NULL) {
			return (ESRCH);
		}

		EMUL_LOCK();
		em = emuldata_get(p);
		head = em->robust_futexes;
		EMUL_UNLOCK();
		/* XXX: ptrace? p_candebug?*/
		if (priv_check(curthread, PRIV_CRED_SETUID) ||
		    priv_check(curthread, PRIV_CRED_SETEUID)/* ||
		    p_candebug(curproc, p) */) {
			PRELE(p);
			return (EPERM);
		}
		PRELE(p);
	}

	error = copyout(&len, args->len, sizeof(l_size_t));
	if (error)
		return (EFAULT);

	error = copyout(head, args->head, sizeof(struct linux_robust_list_head));

	return (error);
}

static int
handle_futex_death(struct proc *p, uint32_t *uaddr, int pi)
{
	uint32_t uval, nval, mval;
	struct futex *f;
	int error;

retry:
	if (copyin(uaddr, &uval, 4))
		return (EFAULT);
	if ((uval & FUTEX_TID_MASK) == p->p_pid) {
		mval = (uval & FUTEX_WAITERS) | FUTEX_OWNER_DIED;
		nval = casuword((u_long *)uaddr, uval, mval);

		if (nval == -1)
			return (EFAULT);

		if (nval != uval)
			goto retry;

		if (!pi && (uval & FUTEX_WAITERS)) {
			error = futex_get(uaddr, NULL, &f,
			    FUTEX_DONTCREATE);
			if (error)
				return (error);
			if (f != NULL) {
				futex_wake(f, 1);
				futex_put(f, NULL);
			}
		}
	}

	return (0);
}

static int
fetch_robust_entry(struct linux_robust_list **entry,
    struct linux_robust_list **head, int *pi)
{
	l_ulong uentry;

	if (copyin((const void *)head, &uentry, sizeof(l_ulong)))
		return (EFAULT);

	*entry = (void *)(uentry & ~1UL);
	*pi = uentry & 1;

	return (0);
}

/* This walks the list of robust futexes releasing them. */
void
release_futexes(struct proc *p)
{
	struct linux_robust_list_head *head = NULL;
	struct linux_robust_list *entry, *next_entry, *pending;
	unsigned int limit = 2048, pi, next_pi, pip;
	struct linux_emuldata *em;
	l_long futex_offset;
	int rc;

	EMUL_LOCK();
	KKASSERT(p != NULL);
	em = emuldata_get(p);
	KKASSERT(em != NULL);
	head = em->robust_futexes;
	EMUL_UNLOCK();

	if (head == NULL)
		return;

	if (fetch_robust_entry(&entry, PTRIN(&head->list.next), &pi))
		return;

	if (copyin(&head->futex_offset, &futex_offset, sizeof(futex_offset)))
		return;

	if (fetch_robust_entry(&pending, PTRIN(&head->pending_list), &pip))
		return;

	while (entry != &head->list) {
		rc = fetch_robust_entry(&next_entry, PTRIN(&entry->next), &next_pi);

		if (entry != pending)
			if (handle_futex_death(p, (uint32_t *)entry + futex_offset, pi))
				return;
		if (rc)
			return;

		entry = next_entry;
		pi = next_pi;

		if (!--limit)
			break;

		/* XXX: not sure about this yield, was sched_relinquish(curthread); */
		lwkt_yield();
	}

	if (pending)
		handle_futex_death(p, (uint32_t *)pending + futex_offset, pip);
}
