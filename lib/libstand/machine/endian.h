/*
 * Copyright (c) 2004 The DragonFly Project.  All rights reserved.
 *
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
 *	from: @(#)endian.h	7.8 (Berkeley) 4/3/91
 * $FreeBSD: src/sys/i386/include/endian.h,v 1.18 1999/12/29 04:33:01 peter Exp $
 */

#ifndef _CPU_ENDIAN_H_
#define	_CPU_ENDIAN_H_

#include <sys/cdefs.h>
#include <machine/stdint.h>

/*
 * Define the order of 32-bit words in 64-bit words.
 */
#define	_QUAD_HIGHWORD 1
#define	_QUAD_LOWWORD 0

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

#define	__htonl(x)	__bswap32(x)
#define	__htons(x)	__bswap16(x)
#define	__ntohl(x)	__bswap32(x)
#define	__ntohs(x)	__bswap16(x)

#define	__byte_swap16_const(x) \
	((((x) & 0xff00) >> 8) | \
	 (((x) & 0x00ff) << 8))

#define	__byte_swap32_const(x) \
	((((x) & 0xff000000) >> 24) | \
	 (((x) & 0x00ff0000) >>  8) | \
	 (((x) & 0x0000ff00) <<  8) | \
	 (((x) & 0x000000ff) << 24))

#define	__byte_swap64_const(x) \
	(((x) >> 56) | (((x) >> 40) & 0xff00) | (((x) >> 24) & 0xff0000) | \
	 (((x) >> 8) & 0xff000000) | (((x) << 8) & ((__uint64_t)0xff << 32)) | \
	 (((x) << 24) & ((__uint64_t)0xff << 40)) | \
	 (((x) << 40) & ((__uint64_t)0xff << 48)) | (((x) << 56)))

#if defined(__INTEL_COMPILER)
# if !defined(__cplusplus) || (defined(__cplusplus) && __INTEL_COMPILER >= 800)
#  define __INTEL_COMPILER_with_DragonFly_endian 1
# endif
#endif

#if defined(__GNUC__) || defined(__INTEL_COMPILER_with_DragonFly_endian)

#define __byte_swap32_var(x) \
	__extension__ ({ register __uint32_t __X = (x); \
	   __asm ("bswap %0" : "+r" (__X)); \
	   __X; })

#define __byte_swap16_var(x) \
	__extension__ ({ register __uint16_t __X = (x); \
	   __asm ("xchgb %h0, %b0" : "+Q" (__X)); \
	   __X; })

#ifdef __OPTIMIZE__

#define	__byte_swap16(x) (__builtin_constant_p(x) ? \
	__byte_swap16_const(x) : __byte_swap16_var(x))

#define	__byte_swap32(x) (__builtin_constant_p(x) ? \
	__byte_swap32_const(x) : __byte_swap32_var(x))

#else	/* __OPTIMIZE__ */

#define __byte_swap16(x) __byte_swap16_var(x)
#define __byte_swap32(x) __byte_swap32_var(x)

#endif	/* __OPTIMIZE__ */

#endif /* __GNUC__ || __INTEL_COMPILER_with_DragonFly_endian */

/*
 * If the compiler-specific part didn't provide this, fallback
 * to the generic versions.
 */

#ifndef __byte_swap16
#define	__byte_swap16(x) __byte_swap16_const(x)
#endif

#ifndef __byte_swap32
#define	__byte_swap32(x) __byte_swap32_const(x)
#endif

#ifndef __byte_swap64
#define	__byte_swap64(x) __byte_swap64_const(x)
#endif

__BEGIN_DECLS

static __inline __uint16_t
__bswap16(__uint16_t _x)
{
	return (__byte_swap16(_x));
}

static __inline __uint32_t
__bswap32(__uint32_t _x)
{
	return (__byte_swap32(_x));
}

static __inline __uint64_t
__bswap64(__uint64_t _x)
{
	return (__byte_swap64(_x));
}

__END_DECLS

#endif /* !_CPU_ENDIAN_H_ */
