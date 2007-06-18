/*
 * Copyright (c) 2007 The DragonFly Project.  All rights reserved.
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
 * $DragonFly: src/sys/platform/vkernel/i386/mp.c,v 1.1 2007/06/18 18:57:12 josepht Exp $
 */

#include <sys/interrupt.h>
#include <sys/types.h>

#include <machine/cpufunc.h>
#include <machine/smp.h>

#if 0
volatile lapic_t lapic; /* needed for kern/kern_shutdown.c */
#endif
volatile u_int           stopped_cpus;
cpumask_t smp_active_mask = 1;  /* which cpus are ready for IPIs etc? */
#if 0
u_int mp_lock;
#endif

void
mp_start(void)
{
	panic("XXX mp_start()");
}

void
mp_announce(void)
{
	panic("XXX mp_announce()");
}

#if 0
void
get_mplock(void)
{
	panic("XXX get_mplock()");
}

int
try_mplock(void)
{
	panic("XXX try_mplock()");
}

void
rel_mplock(void)
{
	panic("XXX rel_mplock()");
}

int
cpu_try_mplock(void)
{
	panic("XXX cpu_try_mplock()");
}
void
cpu_get_initial_mplock(void)
{
	panic("XXX cpu_get_initial_mplock()");
}
#endif


void
forward_fastint_remote(void *arg)
{
	panic("XXX forward_fastint_remote()");
}

void
cpu_send_ipiq(int dcpu)
{
	panic("XXX cpu_send_ipiq()");
}

void
smp_invltlb(void)
{
#ifdef SMP
	panic("XXX smp_invltlb()");
#endif
}

int
stop_cpus(u_int map)
{
	panic("XXX stop_cpus()");
}

int
restart_cpus(u_int map)
{
	panic("XXX restart_cpus()");
}

void
ap_init(void)
{
	panic("XXX ap_init()");
}


