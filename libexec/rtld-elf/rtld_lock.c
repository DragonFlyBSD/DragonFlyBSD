/*-
 * Copyright 1999, 2000 John D. Polstra.
 * All rights reserved.
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
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 *	from: FreeBSD: src/libexec/rtld-elf/sparc64/lockdflt.c,v 1.3 2002/10/09
 * $FreeBSD$
 */

/*
 * Thread locking implementation for the dynamic linker.
 *
 * We use the "simple, non-scalable reader-preference lock" from:
 *
 *   J. M. Mellor-Crummey and M. L. Scott. "Scalable Reader-Writer
 *   Synchronization for Shared-Memory Multiprocessors." 3rd ACM Symp. on
 *   Principles and Practice of Parallel Programming, April 1991.
 *
 * In this algorithm the lock is a single word.  Its low-order bit is
 * set when a writer holds the lock.  The remaining high-order bits
 * contain a count of readers desiring the lock.  The algorithm requires
 * atomic "compare_and_store" and "add" operations, which we implement
 * using assembly language sequences in "rtld_start.S".
 */

#include <sys/param.h>
#include <signal.h>
#include <stdlib.h>
#include <time.h>

#include <stdio.h>
#include <sys/file.h>

#include <machine/sysarch.h>
#include <machine/tls.h>

#include "debug.h"
#include "rtld.h"
#include "rtld_machdep.h"

extern pid_t __sys_getpid(void);

#define WAFLAG		0x1	/* A writer holds the lock */
#define SLFLAG		0x2	/* Sleep pending on lock */
#define RC_INCR		0x4	/* Adjusts count of readers desiring lock */

struct Struct_Lock {
	volatile u_int lock;
	int count;		/* recursion (exclusive) */
	void *owner;		/* owner (exclusive) - tls_get_tcb() */
	sigset_t savesigmask;	/* first exclusive owner sets mask */
} __cachealign;

#define cpu_ccfence()	__asm __volatile("" : : : "memory")

static sigset_t fullsigmask;

struct Struct_Lock phdr_lock;
struct Struct_Lock bind_lock;
struct Struct_Lock libc_lock;

rtld_lock_t	rtld_phdr_lock = &phdr_lock;
rtld_lock_t	rtld_bind_lock = &bind_lock;
rtld_lock_t	rtld_libc_lock = &libc_lock;

static int _rtld_isthreaded;

void _rtld_setthreaded(int threaded);

void
_rtld_setthreaded(int threaded)
{
	_rtld_isthreaded = threaded;
}

static __inline
void *
myid(void)
{
	if (_rtld_isthreaded) {
		return(tls_get_tcb());
	}
	return (void *)(intptr_t)1;
}

void
rlock_acquire(rtld_lock_t lock, RtldLockState *state)
{
	void *tid = myid();
	int v;

	v = lock->lock;
	cpu_ccfence();
	for (;;) {
		if ((v & WAFLAG) == 0) {
			if (atomic_fcmpset_int(&lock->lock, &v, v + RC_INCR)) {
				state->lockstate = RTLD_LOCK_RLOCKED;
				break;
			}
		} else {
			if (lock->owner == tid) {
				++lock->count;
				state->lockstate = RTLD_LOCK_WLOCKED;
				break;
			}
			if (atomic_fcmpset_int(&lock->lock, &v, v | SLFLAG)) {
				umtx_sleep(&lock->lock, v, 0);
			}
		}
		cpu_ccfence();
	}
}

void
wlock_acquire(rtld_lock_t lock, RtldLockState *state)
{
	void *tid = myid();
	sigset_t tmp_oldsigmask;
	int v;

	if (lock->owner == tid) {
		++lock->count;
		state->lockstate = RTLD_LOCK_WLOCKED;
		return;
	}

	sigprocmask(SIG_BLOCK, &fullsigmask, &tmp_oldsigmask);
	v = lock->lock;
	for (;;) {
		if ((v & ~SLFLAG) == 0) {
			if (atomic_fcmpset_int(&lock->lock, &v, WAFLAG))
				break;
		} else {
			if (atomic_fcmpset_int(&lock->lock, &v, v | SLFLAG)) {
				umtx_sleep(&lock->lock, v, 0);
			}
		}
		cpu_ccfence();
	}
	lock->owner = tid;
	lock->count = 1;
	lock->savesigmask = tmp_oldsigmask;
	state->lockstate = RTLD_LOCK_WLOCKED;
}

void
lock_release(rtld_lock_t lock, RtldLockState *state)
{
	sigset_t tmp_oldsigmask;
	int v;

	if (state->lockstate == RTLD_LOCK_UNLOCKED)
		return;
	if ((lock->lock & WAFLAG) == 0) {
		v = atomic_fetchadd_int(&lock->lock, -RC_INCR) - RC_INCR;
		if (v == SLFLAG) {
			atomic_clear_int(&lock->lock, SLFLAG);
			umtx_wakeup(&lock->lock, 0);
		}
	} else if (--lock->count == 0) {
		tmp_oldsigmask = lock->savesigmask;
		lock->owner = NULL;
		v = atomic_fetchadd_int(&lock->lock, -WAFLAG) - WAFLAG;
		if (v == SLFLAG) {
			atomic_clear_int(&lock->lock, SLFLAG);
			umtx_wakeup(&lock->lock, 0);
		}
		sigprocmask(SIG_SETMASK, &tmp_oldsigmask, NULL);
	}
	state->lockstate = RTLD_LOCK_UNLOCKED;
}

static
void
lock_reset(rtld_lock_t lock)
{
	memset(lock, 0, sizeof(*lock));
}

void
lock_upgrade(rtld_lock_t lock, RtldLockState *state)
{
	if (state == NULL)
		return;
	if (state->lockstate == RTLD_LOCK_RLOCKED) {
		lock_release(lock, state);
		wlock_acquire(lock, state);
	}
}

void
lock_restart_for_upgrade(RtldLockState *state)
{
	if (state == NULL)
		return;
	switch (state->lockstate) {
	case RTLD_LOCK_UNLOCKED:
	case RTLD_LOCK_WLOCKED:
		break;
	case RTLD_LOCK_RLOCKED:
		siglongjmp(state->env, 1);
		break;
	default:
		assert(0);
	}
}

void
lockdflt_init(void)
{
	/*
	 * Construct a mask to block all signals except traps which might
	 * conceivably be generated within the dynamic linker itself.
	 */
	sigfillset(&fullsigmask);
	sigdelset(&fullsigmask, SIGILL);
	sigdelset(&fullsigmask, SIGTRAP);
	sigdelset(&fullsigmask, SIGABRT);
	sigdelset(&fullsigmask, SIGEMT);
	sigdelset(&fullsigmask, SIGFPE);
	sigdelset(&fullsigmask, SIGBUS);
	sigdelset(&fullsigmask, SIGSEGV);
	sigdelset(&fullsigmask, SIGSYS);

	_rtld_thread_init(NULL);
}

/*
 * (also called by pthreads)
 */
void
_rtld_thread_init(void *dummy __unused)
{
	lock_reset(rtld_phdr_lock);
	lock_reset(rtld_bind_lock);
	lock_reset(rtld_libc_lock);
}

static RtldLockState fork_states[3];

void
_rtld_thread_prefork(void)
{
	wlock_acquire(rtld_phdr_lock, &fork_states[0]);
	wlock_acquire(rtld_bind_lock, &fork_states[1]);
	wlock_acquire(rtld_libc_lock, &fork_states[2]);
}

void
_rtld_thread_postfork(void)
{
	lock_release(rtld_libc_lock, &fork_states[2]);
	lock_release(rtld_bind_lock, &fork_states[1]);
	lock_release(rtld_phdr_lock, &fork_states[0]);
}

void
_rtld_thread_childfork(void)
{
	sigset_t tmp_oldsigmask;

	lock_reset(rtld_phdr_lock);
	lock_reset(rtld_bind_lock);
	tmp_oldsigmask = rtld_libc_lock->savesigmask;
	lock_reset(rtld_libc_lock);
	sigprocmask(SIG_SETMASK, &tmp_oldsigmask, NULL);
}
