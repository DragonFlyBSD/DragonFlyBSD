/*-
 * Copyright (c) 2003 Matthew Dillon.
 * Copyright (c) 2008 The DragonFly Project.
 * All Rights Reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. The name of the developer may NOT be used to endorse or promote products
 *    derived from this software without specific prior written permission.
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
 *
 * $FreeBSD: src/sys/i386/i386/simplelock.s,v 1.11.2.2 2003/02/04 20:55:28 jhb Exp $
 */

#include <machine/asmacros.h>			/* miscellaneous macros */
#include <machine/lock.h>

/*
 * The spinlock routines may only be used for low level debugging, like
 * kernel printfs, and when no other option is available such as situations
 * relating to hardware interrupt masks.  Spinlock routines should not be
 * used in interrupt service routines or in any other situation.
 *
 * NOTE: for UP the spinlock routines still disable/restore interrupts
 */
ENTRY(spin_lock_deprecated)
	movl	4(%esp),%edx
	SPIN_LOCK((%edx))		/* note: %eax, %ecx tromped */
	ret

ENTRY(spin_unlock_deprecated)
	movl	4(%esp),%edx
	SPIN_UNLOCK((%edx))		/* note: %eax, %ecx tromped */
	ret

NON_GPROF_ENTRY(spin_lock_np)
	movl	4(%esp),%edx
	SPIN_LOCK((%edx))		/* note: %eax, %ecx tromped */
	NON_GPROF_RET

NON_GPROF_ENTRY(spin_unlock_np)
	movl	4(%esp), %edx		/* get the address of the lock */
	SPIN_UNLOCK((%edx))
	NON_GPROF_RET

/*
 * Auxillary convenience routines.  Note that these functions disable and
 * restore interrupts as well, on SMP, as performing spin locking functions.
 */
NON_GPROF_ENTRY(imen_lock)
	SPIN_LOCK(imen_spinlock)
	NON_GPROF_RET

NON_GPROF_ENTRY(imen_unlock)
	SPIN_UNLOCK(imen_spinlock)
	NON_GPROF_RET

NON_GPROF_ENTRY(clock_lock)
	SPIN_LOCK(clock_spinlock)
	NON_GPROF_RET

NON_GPROF_ENTRY(clock_unlock)
	SPIN_UNLOCK(clock_spinlock)
	NON_GPROF_RET

NON_GPROF_ENTRY(com_lock)
	SPIN_LOCK(com_spinlock)
	NON_GPROF_RET

NON_GPROF_ENTRY(com_unlock)
	SPIN_UNLOCK(com_spinlock)
	NON_GPROF_RET
