/*
 * Copyright (c) 2003 Matthew Dillon <dillon@backplane.com>
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
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
 * $DragonFly: src/sys/i386/include/Attic/pmap_inval.h,v 1.1 2004/02/17 19:38:54 dillon Exp $
 */

#ifndef _MACHINE_PMAP_INVAL_H_
#define	_MACHINE_PMAP_INVAL_H_

typedef struct pmap_inval_info {
    int			pir_flags;
    struct lwkt_cpusync	pir_cpusync;
} pmap_inval_info;

typedef pmap_inval_info *pmap_inval_info_t;

#define PIRF_INVLTLB	0x0001	/* request invalidation of whole table */
#define PIRF_INVL1PG	0x0002	/* else request invalidation of one page */
#define PIRF_CPUSYNC	0x0004	/* cpusync is currently active */

#ifdef _KERNEL

void pmap_inval_init(pmap_inval_info_t);
void pmap_inval_add(pmap_inval_info_t, pmap_t, vm_offset_t);
void pmap_inval_flush(pmap_inval_info_t);

#endif

#endif
