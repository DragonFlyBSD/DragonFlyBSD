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
 * $DragonFly: src/sys/netinet/in_cksum.c,v 1.9 2005/01/06 09:14:13 hsu Exp $
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/mbuf.h>
#include <sys/in_cksum.h>

#include <netinet/in.h>
#include <netinet/in_systm.h>
#include <netinet/ip.h>
#include <netinet/ip_var.h>

#include <machine/endian.h>

/*
 * Return the 16 bit 1's complement checksum in network byte order.  Devolve
 * the mbuf into 32 bit aligned segments that we can pass to assembly and
 * do the rest manually.  Even though we return a 16 bit unsigned value,
 * we declare it as a 32 bit unsigned value to reduce unnecessary assembly
 * conversions.
 *
 * Byte ordering issues.  Note two things.  First, no secondary carry occurs,
 * and second, a one's complement checksum is endian-independant.  If we are
 * given a data buffer in network byte order, our checksum will be in network
 * byte order.
 *
 * 0xffff + 0xffff = 0xfffe + C = 0xffff (so no second carry occurs).
 *
 * 0x8142 + 0x8243 = 0x0385 + C = 0x0386 (checksum is in same byte order
 * 0x4281 + 0x4382		= 0x8603  as the data regardless of arch)
 *
 * This works with 16, 32, 64, etc... bits as long as we deal with the
 * carry when collapsing it back down to 16 bits.
 */

__uint32_t
in_cksum_range(struct mbuf *m, int nxt, int offset, int bytes)
{
    __uint8_t *ptr;
    __uint32_t sum0;
    __uint32_t sum1;
    int n;
    int flip;

    sum0 = 0;
    sum1 = 0;
    flip = 0;

    if (nxt != 0) {
	uint32_t sum32;
	struct ipovly ipov;

	/* pseudo header */
	if (offset < sizeof(struct ipovly))
		panic("in_cksum_range: offset too short");
	if (m->m_len < sizeof(struct ip))
		panic("in_cksum_range: bad mbuf chain");
	bzero(&ipov, sizeof ipov);
	ipov.ih_len = htons(bytes);
	ipov.ih_pr = nxt;
	ipov.ih_src = mtod(m, struct ip *)->ip_src;
	ipov.ih_dst = mtod(m, struct ip *)->ip_dst;
	ptr = (uint8_t *)&ipov;

	sum32 = asm_ones32(ptr, sizeof(ipov) / 4);
	sum32 = (sum32 >> 16) + (sum32 & 0xffff);
	if (flip)
	    sum1 += sum32;
	else
	    sum0 += sum32;
    }

    /*
     * Skip fully engulfed mbufs.  Branch predict optimal.
     */
    while (m && offset >= m->m_len) {
	offset -= m->m_len;
	m = m->m_next;
    }

    /*
     * Process the checksum for each segment.  Note that the code below is
     * branch-predict optimal, so it's faster then you might otherwise
     * believe.  When we are buffer-aligned but also odd-byte-aligned from
     * the point of view of the IP packet, we accumulate to sum1 instead of
     * sum0.
     *
     * Initial offsets do not pre-set flip (assert that offset is even?)
     */
    while (bytes > 0 && m) {
	/*
	 * Calculate pointer base and number of bytes to snarf, account
	 * for snarfed bytes.
	 */
	ptr = mtod(m, __uint8_t *) + offset;
	if ((n = m->m_len - offset) > bytes)
	    n = bytes;
	bytes -= n;

	/*
	 * First 16-bit-align our buffer by eating a byte if necessary,
	 * then 32-bit-align our buffer by eating a word if necessary.
	 *
	 * We are endian-sensitive when chomping a byte.  WARNING!  Be
	 * careful optimizing this!  16 ane 32 bit words must be aligned
	 * for this to be generic code.
	 */
	if (((intptr_t)ptr & 1) && n) {
#if BYTE_ORDER == LITTLE_ENDIAN
	    if (flip)
		sum1 += ptr[0];
	    else
		sum0 += ptr[0];
#else
	    if (flip)
		sum0 += ptr[0];
	    else
		sum1 += ptr[0];
#endif
	    ++ptr;
	    --n;
	    flip = 1 - flip;
	}
	if (((intptr_t)ptr & 2) && n > 1) {
	    if (flip)
		sum1 += *(__uint16_t *)ptr;
	    else
		sum0 += *(__uint16_t *)ptr;
	    ptr += 2;
	    n -= 2;
	}

	/*
	 * Process a 32-bit aligned data buffer and accumulate the result
	 * in sum0 or sum1.  Allow only one 16 bit overflow carry.
	 */
	if (n >= 4) {
	    __uint32_t sum32;

	    sum32 = asm_ones32((void *)ptr, n >> 2);
	    sum32 = (sum32 >> 16) + (sum32 & 0xffff);
	    if (flip)
		sum1 += sum32;
	    else
		sum0 += sum32;
	    ptr += n & ~3;
	    /* n &= 3; dontcare */
	}

	/*
	 * Handle oddly-sized buffers.  Handle word issues first while
	 * ptr is still aligned.
	 */
	if (n & 2) {
	    if (flip)
		sum1 += *(__uint16_t *)ptr;
	    else
		sum0 += *(__uint16_t *)ptr;
	    ptr += 2;
	    /* n -= 2; dontcare */
	}
	if (n & 1) {
#if BYTE_ORDER == LITTLE_ENDIAN
	    if (flip)
		sum1 += ptr[0];
	    else
		sum0 += ptr[0];
#else
	    if (flip)
		sum0 += ptr[0];
	    else
		sum1 += ptr[0];
#endif
	    /* ++ptr; dontcare */
	    /* --n; dontcare */
	    flip = 1 - flip;
	}
	m = m->m_next;
	offset = 0;
    }

    /*
     * Due to byte aligned or oddly-sized buffers we may have a checksum
     * in sum1 which needs to be shifted and added to our main sum.  There
     * is a presumption here that no more then 255 overflows occured which
     * is 255/3 byte aligned mbufs in the worst case.
     */
    sum0 += sum1 << 8;
    sum0 = (sum0 >> 16) + (sum0 & 0xffff);
    if (sum0 > 0xffff)
	++sum0;
    return(~sum0 & 0xffff);
}
