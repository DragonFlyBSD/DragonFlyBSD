/*
 * Copyright (c) 2003,2004 The DragonFly Project.  All rights reserved.
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
 * $DragonFly: src/sys/platform/pc64/include/pmap_inval.h,v 1.2 2007/09/24 03:24:45 yanyh Exp $
 */

#ifndef _MACHINE_PMAP_INVAL_H_
#define	_MACHINE_PMAP_INVAL_H_

#ifndef _SYS_THREAD_H_
#include <sys/thread.h>
#endif

typedef struct pmap_inval_info {
    int			pir_flags;
    vm_offset_t		pir_va;
    struct lwkt_cpusync	pir_cpusync;
} pmap_inval_info;

typedef pmap_inval_info *pmap_inval_info_t;

#define PIRF_INVLTLB	0x0001	/* request invalidation of whole table */
#define PIRF_INVL1PG	0x0002	/* else request invalidation of one page */
#define PIRF_CPUSYNC	0x0004	/* cpusync is currently active */
#define PIRF_QUICK	0x0008	/* quick (deinterlock only) */

#ifdef _KERNEL

#ifndef _MACHINE_PMAP_H_
#include <machine/pmap.h>
#endif

void pmap_inval_init(pmap_inval_info_t);
void pmap_inval_interlock(pmap_inval_info_t, pmap_t, vm_offset_t);
void pmap_inval_invltlb(pmap_inval_info_t);
void pmap_inval_deinterlock(pmap_inval_info_t, pmap_t);
void pmap_inval_done(pmap_inval_info_t);

#endif

#endif
