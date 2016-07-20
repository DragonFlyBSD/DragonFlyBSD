/*
 * Copyright (c) 2016 François Tigeot
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice unmodified, this list of conditions, and the following
 *    disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#ifndef _LINUX_BITMAP_H_
#define _LINUX_BITMAP_H_

static inline void
bitmap_or(unsigned long *dst, const unsigned long *src1,
	  const unsigned long *src2, unsigned int nbits)
{
	if (nbits <= BITS_PER_LONG) {
		*dst = *src1 | *src2;
	} else {
		int chunks = DIV_ROUND_UP(nbits, BITS_PER_LONG);

		for (int i = 0;i < chunks;i++)
			dst[i] = src1[i] | src2[i];
	}
}

static inline int
bitmap_weight(unsigned long *bitmap, unsigned int nbits)
{
	unsigned int bit;
	unsigned int retval = 0;

	for_each_set_bit(bit, bitmap, nbits)
		retval++;
	return (retval);
}

#endif	/* _LINUX_BITMAP_H_ */
