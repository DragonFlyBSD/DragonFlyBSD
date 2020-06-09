/*-
 * Copyright (c) 1990 The Regents of the University of California.
 * Copyright (c) 2008 The DragonFly Project.
 * All rights reserved.
 *
 * This code is derived from software contributed to Berkeley by
 * William Jolitz and Don Ahn.
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
 *
 *	from: @(#)clock.c	7.2 (Berkeley) 5/12/91
 * $FreeBSD: src/sys/i386/isa/clock.c,v 1.149.2.6 2002/11/02 04:41:50 iwasaki Exp $
 */

/*
 * TSC cputimer.
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/kernel.h>
#include <sys/sysctl.h>
#include <sys/systimer.h>
#include <sys/globaldata.h>

#include <machine/clock.h>
#include <machine/cputypes.h>

static sysclock_t tsc_cputimer_count_mfence(void);
static sysclock_t tsc_cputimer_count_lfence(void);
static void tsc_cputimer_construct(struct cputimer *, sysclock_t);

static struct cputimer	tsc_cputimer = {
    .next		= SLIST_ENTRY_INITIALIZER,
    .name		= "TSC",
    .pri		= CPUTIMER_PRI_TSC,
    .type		= CPUTIMER_TSC,
    .count		= NULL,	/* determined later */
    .fromhz		= cputimer_default_fromhz,
    .fromus		= cputimer_default_fromus,
    .construct		= tsc_cputimer_construct,
    .destruct		= cputimer_default_destruct,
    .freq		= 0	/* determined later */
};

static struct cpucounter tsc_cpucounter = {
    .freq		= 0,	/* determined later */
    .count		= NULL,	/* determined later */
    .flags		= 0,	/* adjusted later */
    .prio		= CPUCOUNTER_PRIO_TSC,
    .type		= CPUCOUNTER_TSC
};

static void
tsc_cputimer_construct(struct cputimer *timer, sysclock_t oldclock)
{
	timer->base = 0;
	timer->base = oldclock - timer->count();
}

static __inline sysclock_t
tsc_cputimer_count(void)
{
	uint64_t tsc;

	tsc = rdtsc();

	return (tsc + tsc_cputimer.base);
}

static sysclock_t
tsc_cputimer_count_lfence(void)
{
	cpu_lfence();
	return tsc_cputimer_count();
}

static sysclock_t
tsc_cputimer_count_mfence(void)
{
	cpu_mfence();
	return tsc_cputimer_count();
}

static uint64_t
tsc_cpucounter_count_lfence(void)
{
	cpu_lfence();
	return (rdtsc());
}

static uint64_t
tsc_cpucounter_count_mfence(void)
{
	cpu_mfence();
	return (rdtsc());
}

static void
tsc_cputimer_register(void)
{
	uint64_t freq;
	int enable = 1;

	if (!tsc_mpsync) {
#ifndef _KERNEL_VIRTUAL
		if (tsc_invariant) {
			/* Per-cpu cpucounter still works. */
			goto regcnt;
		}
#endif
		return;
	}

	TUNABLE_INT_FETCH("hw.tsc_cputimer_enable", &enable);
	if (!enable)
		return;

	/*
	 * NOTE: We no longer shift the tsc to limit the reported
	 *	 frequency.  We use the actual tsc frequency as-is.
	 */
	freq = tsc_frequency;
	tsc_cputimer.freq = freq;

	kprintf("TSC: cputimer freq %ju\n", (uintmax_t)freq);

	if (cpu_vendor_id == CPU_VENDOR_INTEL)
		tsc_cputimer.count = tsc_cputimer_count_lfence;
	else
		tsc_cputimer.count = tsc_cputimer_count_mfence; /* safe bet */

	cputimer_register(&tsc_cputimer);
	cputimer_select(&tsc_cputimer, 0);

	tsc_cpucounter.flags |= CPUCOUNTER_FLAG_MPSYNC;
#ifndef _KERNEL_VIRTUAL
regcnt:
#endif
	tsc_cpucounter.freq = tsc_frequency;
	if (cpu_vendor_id == CPU_VENDOR_INTEL) {
		tsc_cpucounter.count =
		    tsc_cpucounter_count_lfence;
	} else {
		tsc_cpucounter.count =
		    tsc_cpucounter_count_mfence; /* safe bet */
	}
	cpucounter_register(&tsc_cpucounter);
}
SYSINIT(tsc_cputimer_reg, SI_BOOT2_POST_SMP, SI_ORDER_FIRST,
	tsc_cputimer_register, NULL);
