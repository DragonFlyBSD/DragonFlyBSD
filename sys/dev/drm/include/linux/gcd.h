/*
 * Copyright (c) 2015 Rimvydas Jasinskas
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

/*
 * Standard math function to compute biggest common divisor,
 * based on Euclids algorithm.
 *
 * Long implementation adapted-from:
 * lib/libc/stdlib/getopt_long.c
 *
 * Uses:
 *   64-bit values;
 *   simplified loop form [if(MIN(a,b) == 0) return MAX(a,b)];
 *   swap() arguments just in case..
 */

#ifndef _GCD_H
#define _GCD_H

#include <linux/kernel.h>

static inline uint64_t gcd64(uint64_t a, uint64_t b) __pure2;

/*
 * Compute the greatest common divisor of a and b.
 */
static inline uint64_t
gcd64(uint64_t a, uint64_t b)
{
    uint64_t c;

    if (a < b)
	swap(a, b);

    if (b == 0)
	return a;

    while ((c = a % b) != 0) {
	a = b;
	b = c;
    }

    return (b);
}

#endif /* _GCD_H */
