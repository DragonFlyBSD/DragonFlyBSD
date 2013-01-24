/*
 * Copyright (c) 2013 The DragonFly Project.  All rights reserved.
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
#include <sys/systm.h>

#define        MIN_OUTBLEN     3

/*
 * Take an array of u_char of size inlen and stores its hexadecimal representation
 * into outb which is max size outlen. Use sep as separator.
 *
 * Returns the same outb buffer for ease of use when printing.
 */
char *
hexncpy(const u_char *inb, int inlen, char *outb, int outlen, const char *sep)
{
        char hexdigit[] = "0123456789abcdef";
        char *addr;

        /*
         * In the case we pass invalid buffers or the size
         * of the outb buffer isn't enough to print at least
         * a single u_char in hex format we assert.
         */
        KKASSERT((outb != NULL && inb != NULL && outlen >= MIN_OUTBLEN));

        /* Save up our base address */
        addr = outb;
        for (; inlen > 0 && outlen >= MIN_OUTBLEN; --inlen, outlen -= 3) {
                *outb++ = hexdigit[*inb >> 4];
                *outb++ = hexdigit[*inb++ & 0xf];
                if (sep)
                        *outb++ = *sep;
        }
        *--outb = 0;

        return addr;
}
