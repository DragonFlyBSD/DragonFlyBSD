/*
 * Copyright (c) 2003-2004.  Hiten Pandya <hmp@backplane.com>.
 * 
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following
 *    disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in
 *    the documentation and/or other materials provided with the
 *    distribution.
 *       
 * THIS SOFTWARE IS PROVIDED BY HITEN PANDYA AND CONTRIBUTORS "AS IS" AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL HITEN PANDYA OR CONTRIBUTORS
 * BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR
 * BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
 * WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR
 * OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF
 * ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 * $DragonFly: src/sys/sys/nchstats.h,v 1.1 2004/04/02 05:46:02 hmp Exp $
 */
#ifndef _SYS_NCHSTATS_H_
#define _SYS_NCHSTATS_H_

/*
 * Statistics on the usefulness of namei caches.
 * (per-cpu)
 */
struct	nchstats {
	unsigned long	ncs_goodhits;	/* hits that we can really use */
	unsigned long	ncs_neghits;	/* negative hits that we can use */
	unsigned long	ncs_badhits;	/* hits we must drop */
	unsigned long	ncs_falsehits;	/* hits with id mismatch */
	unsigned long	ncs_miss;   	/* misses */
	unsigned long	ncs_long;   	/* long names that ignore cache */
	unsigned long	ncs_pass2;  	/* names found with passes == 2 */
	unsigned long	ncs_2passes;	/* number of times we attempt it */
};

#endif /* _SYS_NCHSTATS_H_ */
