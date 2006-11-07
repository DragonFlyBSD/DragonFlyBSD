/*
 * Copyright (c) 2003-2006 The DragonFly Project.  All rights reserved.
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
 * $FreeBSD: src/sys/i386/include/lock.h,v 1.11.2.2 2000/09/30 02:49:34 ps Exp $
 * $DragonFly: src/sys/platform/vkernel/include/lock.h,v 1.1 2006/11/07 18:50:07 dillon Exp $
 */

#ifndef _MACHINE_LOCK_H_
#define _MACHINE_LOCK_H_

#ifndef _CPU_PSL_H_
#include <machine/psl.h>
#endif

/*
 * MP_FREE_LOCK is used by both assembly and C under SMP.
 */
#ifdef SMP
#define MP_FREE_LOCK		0xffffffff	/* value of lock when free */
#endif

#ifndef LOCORE

#if defined(_KERNEL) || defined(_UTHREAD)

/*
 * MP LOCK functions for SMP and UP.  Under UP the MP lock does not exist
 * but we leave a few functions intact as macros for convenience.
 */
#ifdef SMP

void	get_mplock(void);
int	try_mplock(void);
void	rel_mplock(void);
int	cpu_try_mplock(void);
void	cpu_get_initial_mplock(void);

extern u_int	mp_lock;

#define MP_LOCK_HELD()   (mp_lock == mycpu->gd_cpuid)
#define ASSERT_MP_LOCK_HELD(td)   KASSERT(MP_LOCK_HELD(), ("MP_LOCK_HELD(): not held thread %p", td))

static __inline void
cpu_rel_mplock(void)
{
	mp_lock = MP_FREE_LOCK;
}

static __inline int
owner_mplock(void)
{
	return (mp_lock);
}

#else

#define get_mplock()
#define try_mplock()	1
#define rel_mplock()
#define owner_mplock()	0	/* always cpu 0 */
#define ASSERT_MP_LOCK_HELD(td)

#endif	/* SMP */
#endif  /* _KERNEL || _UTHREAD */
#endif	/* LOCORE */
#endif	/* !_MACHINE_LOCK_H_ */
