/*
 * Copyright (c) 2005 Jeffrey M. Hsu.  All rights reserved.
 *
 * This code is derived from software contributed to The DragonFly Project
 * by Jeffrey M. Hsu.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
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
 * $DragonFly: src/sys/kern/kern_spinlock.c,v 1.3 2006/05/18 17:53:45 dillon Exp $
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/types.h>
#include <sys/kernel.h>
#include <sys/sysctl.h>
#ifdef INVARIANTS
#include <sys/proc.h>
#endif
#include <ddb/ddb.h>
#include <machine/atomic.h>
#include <machine/cpufunc.h>
#include <machine/clock.h>
#include <sys/spinlock.h>
#include <sys/spinlock2.h>

#define	BACKOFF_INITIAL	1
#define	BACKOFF_LIMIT	256

#ifdef SMP

static void spin_lock_contested_long(struct spinlock *mtx);

#ifdef INVARIANTS
static int spin_lock_test_mode;
#endif

static int64_t spinlocks_contested1;
SYSCTL_QUAD(_debug, OID_AUTO, spinlocks_contested1, CTLFLAG_RD, &spinlocks_contested1, 0, "");
static int64_t spinlocks_contested2;
SYSCTL_QUAD(_debug, OID_AUTO, spinlocks_contested2, CTLFLAG_RD, &spinlocks_contested2, 0, "");

void
spin_lock_contested(struct spinlock *mtx)
{
	int i;
	int backoff = BACKOFF_INITIAL;

	++spinlocks_contested1;
	do {
		/* exponential backoff to reduce contention */
		for (i = 0; i < backoff; i++)
			cpu_nop();
		if (backoff < BACKOFF_LIMIT) {
			backoff <<= 1;
		} else {
			spin_lock_contested_long(mtx);
			return;
		}
		/* do non-bus-locked check first */
	} while (mtx->lock != 0 || atomic_swap_int(&mtx->lock, 1) != 0);
}


/*
 * This code deals with indefinite waits.  If the system is handling a
 * panic we hand the spinlock over to the caller after 1 second.  After
 * 10 seconds we attempt to print a debugger backtrace.  We also run
 * pending interrupts in order to allow a console break into DDB.
 */
static void
spin_lock_contested_long(struct spinlock *mtx)
{
	sysclock_t base;
	sysclock_t count;
	int nsec;

	++spinlocks_contested2;
	base = sys_cputimer->count();
	nsec = 0;

	for (;;) {
		if (mtx->lock == 0 && atomic_swap_int(&mtx->lock, 1) == 0)
			return;
		count = sys_cputimer->count();
		if (count - base > sys_cputimer->freq) {
			printf("spin_lock: %p, indefinite wait!\n", mtx);
			if (panicstr)
				return;
#ifdef INVARIANTS
			if (spin_lock_test_mode) {
				db_print_backtrace();
				return;
			}
#endif
			if (++nsec == 10) {
				nsec = 0;
				db_print_backtrace();
			}
			splz();
			base = count;
		}
	}
}

/*
 * If INVARIANTS is enabled an indefinite wait spinlock test can be
 * executed with 'sysctl debug.spin_lock_test=1'
 */

#ifdef INVARIANTS

static int
sysctl_spin_lock_test(SYSCTL_HANDLER_ARGS)
{
        struct spinlock mtx;
	int error;
	int value = 0;

	if ((error = suser(curthread)) != 0)
		return (error);
	if ((error = SYSCTL_IN(req, &value, sizeof(value))) != 0)
		return (error);

	if (value == 1) {
		mtx.lock = 1;
		spin_lock_test_mode = 1;
		spin_lock(&mtx);
		spin_unlock(&mtx);
		spin_lock_test_mode = 0;
	}
        return (0);
}

SYSCTL_PROC(_debug, KERN_PROC_ALL, spin_lock_test, CTLFLAG_RW|CTLTYPE_INT,
        0, 0, sysctl_spin_lock_test, "I", "Test spinlock wait code");


#endif

#endif
