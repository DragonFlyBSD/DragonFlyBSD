/*-
 * Copyright (c) 1987, 1991 Regents of the University of California.
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
 * 3. Neither the name of the University nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific, prior written permission.
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
 *	@(#)endian.h	7.8 (Berkeley) 4/3/91
 * $FreeBSD: src/sys/amd64/include/endian.h,v 1.5 2003/09/22 22:37:49 peter Exp $
 */

#ifndef _CPU_ENDIAN_H_
#define	_CPU_ENDIAN_H_

#include <sys/cdefs.h>

typedef unsigned char	__uint8_t;
typedef unsigned short	__uint16_t;
typedef unsigned int	__uint32_t;
typedef unsigned long	__uint64_t;

/*
 * Definitions for byte order, according to byte significance from low
 * address to high.
 */
#define	_LITTLE_ENDIAN	1234	/* LSB first: i386, vax */
#define	_BIG_ENDIAN	4321	/* MSB first: 68000, ibm, net */
#define	_PDP_ENDIAN	3412	/* LSB first in word, MSW first in long */

#define	_BYTE_ORDER	_LITTLE_ENDIAN

/*
 * Deprecated variants that don't have enough underscores to be useful in more
 * strict namespaces.
 */
#if __BSD_VISIBLE
#define	LITTLE_ENDIAN	_LITTLE_ENDIAN
#define	BIG_ENDIAN	_BIG_ENDIAN
#define	PDP_ENDIAN	_PDP_ENDIAN
#define	BYTE_ORDER	_BYTE_ORDER
#endif

static __inline __uint64_t
__bswap64(__uint64_t _x)
{
	return (__builtin_bswap64(_x));
}

static __inline __uint32_t
__bswap32(__uint32_t _x)
{
	return (__builtin_bswap32(_x));
}

static __inline __uint16_t
__bswap16(__uint16_t _x)
{
	return (__builtin_bswap16(_x));
}

#define	__htonl(x)	__bswap32(x)
#define	__htons(x)	__bswap16(x)
#define	__ntohl(x)	__bswap32(x)
#define	__ntohs(x)	__bswap16(x)

/*
 * Userland compatibility double underscore variants to help with DPorts
 * (injected through <sys/types.h>).
 */
#ifndef _KERNEL
#define	__LITTLE_ENDIAN	_LITTLE_ENDIAN
#define	__BIG_ENDIAN	_BIG_ENDIAN
#define	__PDP_ENDIAN	_PDP_ENDIAN
#define	__BYTE_ORDER	_BYTE_ORDER

#ifndef __FLOAT_WORD_ORDER
#define	__FLOAT_WORD_ORDER __BYTE_ORDER
#endif
#endif /* !_KERNEL */

#endif /* !_CPU_ENDIAN_H_ */
