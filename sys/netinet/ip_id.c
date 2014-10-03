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
 * $DragonFly: src/sys/netinet/ip_id.c,v 1.8 2007/11/23 22:29:51 dillon Exp $
 */

/*
 * Random ip sequence number generator.  Use the system PRNG to shuffle the
 * 65536 entry ID space.  We reshuffle the front-side of the array as we
 * index through it, guarenteeing an id will not be reused for at least
 * 32768 calls.
 */
#include <sys/param.h>
#include <sys/systm.h>
#include <sys/kernel.h>
#include <sys/random.h>
#include <sys/spinlock.h>
#include <sys/globaldata.h>
#include <netinet/ip_var.h>

#include <sys/spinlock2.h>

#define IPRANDCOUNT	32

typedef struct iprandinfo {
	short	randdata[IPRANDCOUNT];
	int	randidx;
	int	isidx;
} *iprandinfo_t;

struct iprandinfo iprandcpu[MAXCPU];

static u_int16_t ip_shuffle[65536];

/*
 * Initialize the shuffle.  We assume that the system PRNG won't be all that
 * good this early in the boot sequence but use it anyway.  The ids will be
 * reshuffled as they are popped and the PRNG should be better then.
 */
static void
ip_initshuffle(void *dummy __unused)
{
	int i;

	for (i = 0; i < 65536; ++i)
		ip_shuffle[i] = i;
	for (i = 0; i < 65536; ++i)
		ip_randomid();
	for (i = 0; i < ncpus; ++i) {
		iprandcpu[i].isidx = i * (65536 / ncpus_fit);
	}
}

SYSINIT(ipshuffle, SI_SUB_PSEUDO, SI_ORDER_ANY, ip_initshuffle, NULL);

/*
 * Return a random IP id.  Use a forward shuffle over half the index
 * space to avoid duplicates occuring too quickly.  Since the initial
 * shuffle may not have had a good random basis we returned the element
 * at the shuffle target instead of the current element.
 *
 * XXX make per-cpu so the spinlock can be removed?
 */
u_int16_t
ip_randomid(void)
{
	globaldata_t gd = mycpu;
	iprandinfo_t info;
	u_int16_t si, r;
	int i1, i2;

	info = &iprandcpu[gd->gd_cpuid];
	crit_enter();

	/*
	 * Reload random array efficiently
	 */
	if (info->randidx == 0) {
		info->randidx = IPRANDCOUNT;
		read_random_unlimited(info->randdata, sizeof(info->randdata));
	}

	/*
	 * Get random number (si) and calculate shuffle.  The shuffle
	 * is calculated such that the cpus do not interfere with each
	 * other.
	 */
	si = info->randdata[--info->randidx];
	i1 = (info->isidx & 0xFFFF & ~ncpus_fit_mask) | gd->gd_cpuid;
	i2 = ((i1 + (si & 0x7FFF)) & 0xFFFF & ~ncpus_fit_mask) | gd->gd_cpuid;

	/*
	 * Do the shuffle, bump isidx taking into account the cpu
	 * partitioning.
	 */
	r = ip_shuffle[i2];
	ip_shuffle[i2] = ip_shuffle[i1];
	ip_shuffle[i1] = r;
	info->isidx += ncpus_fit;

	crit_exit();
	return(r);
}
