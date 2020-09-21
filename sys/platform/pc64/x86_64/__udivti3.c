/*-
 * This file is dual licensed under the MIT and the University of Illinois
 * Open Source Licenses.
 *
 *-
 * University of Illinois/NCSA
 * Open Source License
 *
 * Copyright (c) 2009-2014 by the contributors listed below
 *
 * All rights reserved.
 *
 * Developed by:
 *
 * LLVM Team
 *
 * University of Illinois at Urbana-Champaign
 *
 * http://llvm.org
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal with the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * * Redistributions of source code must retain the above copyright notice,
 *   this list of conditions and the following disclaimers.
 *
 * * Redistributions in binary form must reproduce the above copyright notice,
 *   this list of conditions and the following disclaimers in the
 *   documentation and/or other materials provided with the distribution.
 *
 * * Neither the names of the LLVM Team, University of Illinois at
 *   Urbana-Champaign, nor the names of its contributors may be used to
 *   endorse or promote products derived from this Software without specific
 *   prior written permission.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL THE
 * CONTRIBUTORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
 * WITH THE SOFTWARE.
 *
 *-
 * Copyright (c) 2009-2014 by the contributors listed below
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to
 * deal in the Software without restriction, including without limitation the
 * rights to use, copy, modify, merge, publish, distribute, sublicense, and/or
 * sell copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 *
 * -
 * List of contributors:
 *
 * Craig van Vliet <cvanvliet@auroraux.org>
 * Edward O'Callaghan <eocallaghan@auroraux.org>
 * Howard Hinnant <hhinnant@apple.com>
 * Matt Thomas <matt@NetBSD.org>
 * Joerg Sonnenberger <joerg@NetBSD.org>
 */

#include <machine/limits.h>
#include <machine/stdint.h>

typedef union
{
	_uint128_t all;
	struct {
		unsigned long low;
		unsigned long high;
	} s;
} utwords;

/* Effects: if rem != 0, *rem = a % b
 * Returns: a / b
 */

/* Translated from Figure 3-40 of The PowerPC Compiler Writer's Guide */

static _uint128_t
__udivmodti4(_uint128_t a, _uint128_t b, _uint128_t *rem)
{
	const unsigned n_udword_bits = sizeof(unsigned long) * CHAR_BIT;
	const unsigned n_utword_bits = sizeof(_uint128_t) * CHAR_BIT;
	utwords n, d, q, r;
	unsigned sr;

	n.all = a;
	d.all = b;

	/* special cases, X is unknown, K != 0 */
	if (n.s.high == 0) {
		if (d.s.high == 0) {
			/* 0 X
			 * ---
			 * 0 X
			 */
			if (rem)
				*rem = n.s.low % d.s.low;
			return n.s.low / d.s.low;
		}
		/* 0 X
		 * ---
		 * K X
		 */
		if (rem)
			*rem = n.s.low;
		return 0;
	}
	/* n.s.high != 0 */
	if (d.s.low == 0) {
		if (d.s.high == 0) {
			/* K X
			 * ---
			 * 0 0
			 */
			if (rem)
				*rem = n.s.high % d.s.low;
			return n.s.high / d.s.low;
		}
		/* d.s.high != 0 */
		if (n.s.low == 0) {
			/* K 0
			 * ---
			 * K 0
			 */
			if (rem) {
				r.s.high = n.s.high % d.s.high;
				r.s.low = 0;
				*rem = r.all;
			}
			return n.s.high / d.s.high;
		}
		/* K K
		 * ---
		 * K 0
		 */
		if ((d.s.high & (d.s.high - 1)) == 0) { /* if d is a power of 2 */
			if (rem) {
				r.s.low = n.s.low;
				r.s.high = n.s.high & (d.s.high - 1);
				*rem = r.all;
			}
			return n.s.high >> __builtin_ctzl(d.s.high);
		}
		/* K K
		 * ---
		 * K 0
		 */
		sr = __builtin_clzl(d.s.high) - __builtin_clzl(n.s.high);
		/* 0 <= sr <= n_udword_bits - 2 or sr large */
		if (sr > n_udword_bits - 2) {
			if (rem)
				*rem = n.all;
			return 0;
		}
		++sr;
		/* 1 <= sr <= n_udword_bits - 1 */
		/* q.all = n.all << (n_utword_bits - sr); */
		q.s.low = 0;
		q.s.high = n.s.low << (n_udword_bits - sr);
		/* r.all = n.all >> sr; */
		r.s.high = n.s.high >> sr;
		r.s.low = (n.s.high << (n_udword_bits - sr)) | (n.s.low >> sr);
	} else {  /* d.s.low != 0 */
		if (d.s.high == 0) {
			/* K X
			 * ---
			 * 0 K
			 */
			if ((d.s.low & (d.s.low - 1)) == 0) { /* if d is a power of 2 */
				if (rem)
					*rem = n.s.low & (d.s.low - 1);
				if (d.s.low == 1)
					return n.all;
				sr = __builtin_ctzl(d.s.low);
				q.s.high = n.s.high >> sr;
				q.s.low = (n.s.high << (n_udword_bits - sr)) |
				    (n.s.low >> sr);
				return q.all;
			}
			/* K X
			 * ---
			 * 0 K
			 */
			sr = 1 + n_udword_bits + __builtin_clzl(d.s.low) -
			    __builtin_clzl(n.s.high);
			/* 2 <= sr <= n_utword_bits - 1
			 * q.all = n.all << (n_utword_bits - sr);
			 * r.all = n.all >> sr;
			 * if (sr == n_udword_bits)
			 * {
			 *     q.s.low = 0;
			 *     q.s.high = n.s.low;
			 *     r.s.high = 0;
			 *     r.s.low = n.s.high;
			 * }
			 * else if (sr < n_udword_bits)  // 2 <= sr <= n_udword_bits - 1
			 * {
			 *     q.s.low = 0;
			 *     q.s.high = n.s.low << (n_udword_bits - sr);
			 *     r.s.high = n.s.high >> sr;
			 *     r.s.low = (n.s.high << (n_udword_bits - sr)) | (n.s.low >> sr);
			 * }
			 * else              // n_udword_bits + 1 <= sr <= n_utword_bits - 1
			 * {
			 *     q.s.low = n.s.low << (n_utword_bits - sr);
			 *     q.s.high = (n.s.high << (n_utword_bits - sr)) |
			 *              (n.s.low >> (sr - n_udword_bits));
			 *     r.s.high = 0;
			 *     r.s.low = n.s.high >> (sr - n_udword_bits);
			 * }
			 */
			q.s.low =  (n.s.low << (n_utword_bits - sr)) &
			    ((long)(int)(n_udword_bits - sr) >> (n_udword_bits-1));
			q.s.high = ((n.s.low << ( n_udword_bits - sr)) &
			    ((long)(int)(sr - n_udword_bits - 1) >> (n_udword_bits-1))) |
			    (((n.s.high << (n_utword_bits - sr)) |
				(n.s.low >> (sr - n_udword_bits))) &
				((long)(int)(n_udword_bits - sr) >> (n_udword_bits-1)));
			r.s.high = (n.s.high >> sr) &
			    ((long)(int)(sr - n_udword_bits) >> (n_udword_bits-1));
			r.s.low =  ((n.s.high >> (sr - n_udword_bits)) &
			    ((long)(int)(n_udword_bits - sr - 1) >> (n_udword_bits-1))) |
			    (((n.s.high << (n_udword_bits - sr)) |
				(n.s.low >> sr)) &
				((long)(int)(sr - n_udword_bits) >> (n_udword_bits-1)));
		} else {
			/* K X
			 * ---
			 * K K
			 */
			sr = __builtin_clzl(d.s.high) - __builtin_clzl(n.s.high);
			/*0 <= sr <= n_udword_bits - 1 or sr large */
			if (sr > n_udword_bits - 1) {
				if (rem)
					*rem = n.all;
				return 0;
			}
			++sr;
			/* 1 <= sr <= n_udword_bits */
			/* q.all = n.all << (n_utword_bits - sr); */
			q.s.low = 0;
			q.s.high = n.s.low << (n_udword_bits - sr);
			/* r.all = n.all >> sr;
			 * if (sr < n_udword_bits)
			 * {
			 *     r.s.high = n.s.high >> sr;
			 *     r.s.low = (n.s.high << (n_udword_bits - sr)) | (n.s.low >> sr);
			 * }
			 * else
			 * {
			 *     r.s.high = 0;
			 *     r.s.low = n.s.high;
			 * }
			 */
			r.s.high = (n.s.high >> sr) &
			    ((long)(int)(sr - n_udword_bits) >> (n_udword_bits-1));
			r.s.low = (n.s.high << (n_udword_bits - sr)) |
			    ((n.s.low >> sr) &
				((long)(int)(sr - n_udword_bits) >> (n_udword_bits-1)));
		}
	}
	/* Not a special case
	 * q and r are initialized with:
	 * q.all = n.all << (n_utword_bits - sr);
	 * r.all = n.all >> sr;
	 * 1 <= sr <= n_utword_bits - 1
	 */
	unsigned carry = 0;
	for (; sr > 0; --sr) {
		/* r:q = ((r:q)  << 1) | carry */
		r.s.high = (r.s.high << 1) | (r.s.low  >> (n_udword_bits - 1));
		r.s.low  = (r.s.low  << 1) | (q.s.high >> (n_udword_bits - 1));
		q.s.high = (q.s.high << 1) | (q.s.low  >> (n_udword_bits - 1));
		q.s.low  = (q.s.low  << 1) | carry;
		/* carry = 0;
		 * if (r.all >= d.all)
		 * {
		 *     r.all -= d.all;
		 *      carry = 1;
		 * }
		 */
		const _int128_t s =
		    (_int128_t)(d.all - r.all - 1) >> (n_utword_bits - 1);
		carry = s & 1;
		r.all -= d.all & s;
	}
	q.all = (q.all << 1) | carry;
	if (rem)
		*rem = r.all;
	return q.all;
}

_uint128_t __udivti3(_uint128_t a, _uint128_t b);

_uint128_t
__udivti3(_uint128_t a, _uint128_t b)
{
	return __udivmodti4(a, b, 0);
}
