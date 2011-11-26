/*
 * Copyright (c) 2003, 2004 The DragonFly Project.  All rights reserved.
 *
 * This code is derived from software contributed to The DragonFly Project
 * by Hiten Pandya <hmp@backplane.com>.
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
 * $DragonFly: src/lib/libkvm/kvm_util.c,v 1.3 2004/07/16 05:04:36 hmp Exp $
 */

/*
 * Useful functions that are used across the source base for
 * various purpose.
 */

#include <sys/param.h>
#include <sys/nchstats.h>
#include <sys/sysctl.h>

#include <stdio.h>
#include <stdlib.h>
#include <strings.h>
#include <err.h>

/*
 * Aggregate the per-cpu counters we retrieved via sysctl(2)
 * to give the total across the CPUs.  Use a nasty trick to
 * aggregate the counters in the structure! YYY
 */
void
kvm_nch_cpuagg(struct nchstats *unagg, struct nchstats *ttl, int cpucnt)
{
	int i, off, siz;
	siz = sizeof(struct nchstats);

	if (!unagg && !ttl)
		return;

	bzero(ttl, siz);
	
	/* kick hmp@ for this nasty loop! :-) */
	for (i = 0; i < cpucnt; ++i) {
		for (off = 0; off < siz; off += sizeof(u_long)) {
			*(u_long *)((char *)(*(&ttl)) + off) +=
			*(u_long *)((char *)&unagg[i] + off);
		}
	}
}
