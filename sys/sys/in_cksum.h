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
 *	(note: completely rewritten for DragonFly)
 *
 *	from tahoe:	in_cksum.c	1.2	86/01/05
 *	from:		@(#)in_cksum.c	1.3 (Berkeley) 1/19/91
 *	from: Id: in_cksum.c,v 1.8 1995/12/03 18:35:19 bde Exp
 * $FreeBSD: src/sys/i386/include/in_cksum.h,v 1.7.2.2 2002/07/02 04:03:04 jdp Exp $
 */

#ifndef _SYS_IN_CKSUM_H_
#define	_SYS_IN_CKSUM_H_

#ifndef _SYS_TYPES_H_
#include <sys/types.h>
#endif
#include <machine/stdint.h>

#ifdef _KERNEL

struct ip;
struct mbuf;

__uint32_t in_cksum_range(const struct mbuf *m, int nxt, int offset, int bytes);
__uint32_t asm_ones32(const void *buf, int count);	/* in 32 bit words */

static __inline u_int
in_cksum(const struct mbuf *m, int len)
{
    return(in_cksum_range(m, 0, 0, len));
}

static __inline u_int
in_cksum_skip(const struct mbuf *m, int len, int skip)
{
    return(in_cksum_range(m, 0, skip, len - skip));
}

static __inline u_int
in_cksum_hdr(const struct ip *ip)
{
    __uint32_t sum;

    sum = asm_ones32((const void *)ip, 5);	/* 5x4 = 20 bytes */
    sum = (sum >> 16) + (sum & 0xFFFF);
    if (sum > 0xFFFF)
	++sum;
    return(~sum & 0xFFFF);
}

#endif

static __inline u_short
in_addword(u_short sum, u_short b)
{
    /* __volatile is necessary because the condition codes are used. */
    __asm __volatile ("addw %1, %0; adcw $0,%0" : "+r" (sum) : "r" (b));

    return (sum);
}

static __inline u_short
in_pseudo(u_int sum, u_int b, u_int c)
{
    /* __volatile is necessary because the condition codes are used. */
    __asm __volatile ("addl %1,%0; adcl %2,%0; adcl $0,%0" 
			: "+r" (sum) 
			: "g" (b), "g" (c));
    sum = (sum & 0xffff) + (sum >> 16);
    if (sum > 0xffff)
	sum -= 0xffff;
    return (sum);
}

#endif /* _MACHINE_IN_CKSUM_H_ */
