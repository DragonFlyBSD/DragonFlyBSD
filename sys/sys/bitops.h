/*
 * Copyright (c) 2004, 2005 David Young.  All rights reserved.
 *
 * Programmed for NetBSD by David Young.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. The name of David Young may not be used to endorse or promote
 *    products derived from this software without specific prior
 *    written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY David Young ``AS IS'' AND ANY
 * EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO,
 * THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A
 * PARTICULAR PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL David
 * Young BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
 * EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED
 * TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
 * ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
 * OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY
 * OF SUCH DAMAGE.
 *
 * $DragonFly: src/sys/sys/bitops.h,v 1.1 2007/10/14 04:15:17 sephe Exp $
 */

#ifndef _SYS_BITOPS_H_
#define _SYS_BITOPS_H_

/*
 * __BIT(n): Return a bitmask with bit n set, where the least
 *           significant bit is bit 0.
 *
 * __BITS(m, n): Return a bitmask with bits m through n, inclusive,
 *               set.  It does not matter whether m>n or m<=n.  The
 *               least significant bit is bit 0.
 *
 * A "bitfield" is a span of consecutive bits defined by a bitmask,
 * where 1s select the bits in the bitfield.  __SHIFTIN, __SHIFTOUT,
 * and __SHIFTOUT_MASK help read and write bitfields from device
 * registers.
 *
 * __SHIFTIN(v, mask): Left-shift bits `v' into the bitfield
 *                     defined by `mask', and return them.  No
 *                     side-effects.
 *
 * __SHIFTOUT(v, mask): Extract and return the bitfield selected
 *                      by `mask' from `v', right-shifting the
 *                      bits so that the rightmost selected bit
 *                      is at bit 0.  No side-effects.
 *
 * __SHIFTOUT_MASK(mask): Right-shift the bits in `mask' so that
 *                        the rightmost non-zero bit is at bit
 *                        0.  This is useful for finding the
 *                        greatest unsigned value that a bitfield
 *                        can hold.  No side-effects.  Note that
 *                        __SHIFTOUT_MASK(m) = __SHIFTOUT(m, m).
 */

/* __BIT(n): nth bit, where __BIT(0) == 0x1. */
#define	__BIT(__n) (((__n) == 32) ? 0 : ((uint32_t)1 << (__n)))
#define	__BIT64(__n) (((__n) == 64) ? 0 : ((uint64_t)1 << (__n)))

/* __BITS(m, n): bits m through n, m < n. */
#define	__BITS(__m, __n)	\
	((__BIT(MAX((__m), (__n)) + 1) - 1) ^ (__BIT(MIN((__m), (__n))) - 1))

#define	__BITS64(__m, __n)	\
	((__BIT64(MAX((__m), (__n)) + 1) - 1) ^ \
	 (__BIT64(MIN((__m), (__n))) - 1))

/* Find least significant bit that is set */
#define	__LOWEST_SET_BIT(__mask) ((((__mask) - 1) & (__mask)) ^ (__mask))

#define	__SHIFTOUT(__x, __mask) (((__x) & (__mask)) / __LOWEST_SET_BIT(__mask))
#define	__SHIFTIN(__x, __mask) ((__x) * __LOWEST_SET_BIT(__mask))
#define	__SHIFTOUT_MASK(__mask) __SHIFTOUT((__mask), (__mask))

#endif	/* !_SYS_BITOPS_H_ */
