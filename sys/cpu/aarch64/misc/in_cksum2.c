/*
 * Copyright (c) 2003,2004,2008 The DragonFly Project.  All rights reserved.
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
 */

#include <sys/types.h>
#include <sys/in_cksum.h>

/*
 * asm_ones32(32bitalignedbuffer, numberof32bitwords)
 *
 * Returns the 32 bit one complement partial checksum.  This is
 * basically a 1's complement checksum without the inversion (~)
 * at the end.  A 32 bit value is returned.  If the caller is
 * calculating a 16 bit 1's complement checksum the caller must
 * collapse the 32 bit return value via:
 *
 *	result = (result >> 16) + (result & 0xFFFF)
 *	if (result > 0xFFFF)
 *	    result -= 0xFFFF;	<<< same as (result + 1) & 0xFFFF
 *				    within the range of result.
 *
 * Note that worst case 0xFFFFFFFF + 0xFFFFFFFF = 0xFFFFFFFE + CARRY,
 * so no double-carry ever occurs.
 */
__uint32_t
asm_ones32(const void *buf, int count)
{
	const __uint32_t *ptr = buf;
	__uint64_t sum = 0;

	while (count >= 5) {
		sum += ptr[0];
		sum += ptr[1];
		sum += ptr[2];
		sum += ptr[3];
		sum += ptr[4];
		ptr += 5;
		count -= 5;
	}
	while (count > 0) {
		sum += *ptr++;
		count--;
	}

	/* Fold 64-bit sum to 32-bit with carry */
	sum = (sum >> 32) + (sum & 0xffffffff);
	if (sum > 0xffffffff)
		sum -= 0xffffffff;

	return ((__uint32_t)sum);
}
