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
 * $DragonFly: src/sys/sys/nchstats.h,v 1.3 2004/07/16 05:04:36 hmp Exp $
 */
#ifndef _SYS_NCHSTATS_H_
#define _SYS_NCHSTATS_H_

/*
 * Statistics on the usefulness of namei caches.
 * (per-cpu)
 *
 * Allocated in an array so make sure this is cache-aligned.
 */
struct	nchstats {
	unsigned long	ncs_goodhits;	/* hits that we can really use */
	unsigned long	ncs_neghits;	/* negative hits that we can use */
	unsigned long	ncs_badhits;	/* hits we must drop */
	unsigned long	ncs_falsehits;	/* hits with id mismatch */
	unsigned long	ncs_miss;   	/* misses */
	unsigned long	ncs_longhits;  	/* path lookup hits */
	unsigned long	ncs_longmiss;	/* path lookup misses */
	unsigned long	ncs_unused;	/* number of times we attempt it */
} __cachealign;

#endif /* _SYS_NCHSTATS_H_ */
