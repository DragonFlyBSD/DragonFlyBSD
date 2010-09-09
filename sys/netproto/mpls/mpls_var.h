/*
 * Copyright (c) 2007 The DragonFly Project.  All rights reserved.
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
 * $DragonFly: src/sys/netproto/mpls/mpls_var.h,v 1.2 2008/08/05 15:11:32 nant Exp $
 */

#ifndef _NETMPLS_MPLS_VAR_H_
#define _NETMPLS_MPLS_VAR_H_

#include <sys/types.h>
#include <net/route.h>
#include <netproto/mpls/mpls.h>

struct mpls_stats {
	u_long	mplss_total;
	u_long	mplss_tooshort;
	u_long	mplss_toosmall;
	u_long	mplss_invalid;
	u_long	mplss_reserved;
	u_long	mplss_cantforward;
	u_long	mplss_forwarded;
	u_long	mplss_ttlexpired;
};

#ifdef _KERNEL

#if defined(SMP)
#define mplsstat mplsstats_percpu[mycpuid]
#else /* !SMP */
#define mplsstat mplsstats_percpu[0]
#endif

extern struct mpls_stats  mplsstats_percpu[MAXCPU];

void			mpls_init(void);
void			mpls_cpufn(struct mbuf **, int);
void			mpls_input(struct mbuf *);
int			mpls_output(struct mbuf *, struct rtentry *);
boolean_t		mpls_output_process(struct mbuf *, struct rtentry *);

#endif	/* _KERNEL */

#endif	/* _NETMPLS_MPLS_VAR_H_ */
