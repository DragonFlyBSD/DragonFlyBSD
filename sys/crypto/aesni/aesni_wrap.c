/*-
 * Copyright (c) 2010 Konstantin Belousov <kib@FreeBSD.org>
 * Copyright (c) 2010 Pawel Jakub Dawidek <pjd@FreeBSD.org>
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
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHORS AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHORS OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 * $FreeBSD: src/sys/crypto/aesni/aesni_wrap.c,v 1.7 2010/11/27 15:41:44 kib Exp $
 */

#include <sys/systm.h>
#include <crypto/aesni/aesni.h>

void
aesni_encrypt_cbc(int rounds, const void *key_schedule, size_t len,
    const uint8_t *from, uint8_t *to, const uint8_t iv[AES_BLOCK_LEN])
{
	const uint8_t *ivp;
	size_t i;

	len /= AES_BLOCK_LEN;
	ivp = iv;
	for (i = 0; i < len; i++) {
		aesni_enc(rounds - 1, key_schedule, from, to, ivp);
		ivp = to;
		from += AES_BLOCK_LEN;
		to += AES_BLOCK_LEN;
	}
}

void
aesni_encrypt_ecb(int rounds, const void *key_schedule, size_t len,
    const uint8_t from[AES_BLOCK_LEN], uint8_t to[AES_BLOCK_LEN])
{
	size_t i;

	len /= AES_BLOCK_LEN;
	for (i = 0; i < len; i++) {
		aesni_enc(rounds - 1, key_schedule, from, to, NULL);
		from += AES_BLOCK_LEN;
		to += AES_BLOCK_LEN;
	}
}

void
aesni_decrypt_ecb(int rounds, const void *key_schedule, size_t len,
    const uint8_t from[AES_BLOCK_LEN], uint8_t to[AES_BLOCK_LEN])
{
	size_t i;

	len /= AES_BLOCK_LEN;
	for (i = 0; i < len; i++) {
		aesni_dec(rounds - 1, key_schedule, from, to, NULL);
		from += AES_BLOCK_LEN;
		to += AES_BLOCK_LEN;
	}
}

#define	AES_XTS_BLOCKSIZE	16
#define	AES_XTS_IVSIZE		8
#define	AES_XTS_ALPHA		0x87	/* GF(2^128) generator polynomial */

void
aesni_crypt_xts_block(int rounds, const void *key_schedule, uint8_t *tweak,
    const uint8_t *from, uint8_t *to, int do_encrypt)
{
	uint8_t block[AES_XTS_BLOCKSIZE];
	u_int i, carry_in, carry_out;

	for (i = 0; i < AES_XTS_BLOCKSIZE; i++)
		block[i] = from[i] ^ tweak[i];

	if (do_encrypt)
		aesni_enc(rounds - 1, key_schedule, block, to, NULL);
	else
		aesni_dec(rounds - 1, key_schedule, block, to, NULL);

	for (i = 0; i < AES_XTS_BLOCKSIZE; i++)
		to[i] ^= tweak[i];

	/* Exponentiate tweak. */
	carry_in = 0;
	for (i = 0; i < AES_XTS_BLOCKSIZE; i++) {
		carry_out = tweak[i] & 0x80;
		tweak[i] = (tweak[i] << 1) | (carry_in ? 1 : 0);
		carry_in = carry_out;
	}
	if (carry_in)
		tweak[0] ^= AES_XTS_ALPHA;
	bzero(block, sizeof(block));
}

void
aesni_crypt_xts(int rounds, const void *data_schedule,
    const void *tweak_schedule, size_t len, const uint8_t *from, uint8_t *to,
    const uint8_t iv[AES_BLOCK_LEN], int do_encrypt)
{
	uint8_t tweak[AES_XTS_BLOCKSIZE];
	uint64_t blocknum;
	size_t i;

	/*
	 * Prepare tweak as E_k2(IV). IV is specified as LE representation
	 * of a 64-bit block number which we allow to be passed in directly.
	 */
	bcopy(iv, &blocknum, AES_XTS_IVSIZE);
	for (i = 0; i < AES_XTS_IVSIZE; i++) {
		tweak[i] = blocknum & 0xff;
		blocknum >>= 8;
	}
	/* Last 64 bits of IV are always zero. */
	bzero(tweak + AES_XTS_IVSIZE, AES_XTS_IVSIZE);
	aesni_enc(rounds - 1, tweak_schedule, tweak, tweak, NULL);

	len /= AES_XTS_BLOCKSIZE;
	for (i = 0; i < len; i++) {
		aesni_crypt_xts_block(rounds, data_schedule, tweak, from, to,
		    do_encrypt);
		from += AES_XTS_BLOCKSIZE;
		to += AES_XTS_BLOCKSIZE;
	}

	bzero(tweak, sizeof(tweak));
}

void
aesni_encrypt_xts(int rounds, const void *data_schedule,
    const void *tweak_schedule, size_t len, const uint8_t *from, uint8_t *to,
    const uint8_t iv[AES_BLOCK_LEN])
{

	aesni_crypt_xts(rounds, data_schedule, tweak_schedule, len, from, to,
	    iv, 1);
}

void
aesni_decrypt_xts(int rounds, const void *data_schedule,
    const void *tweak_schedule, size_t len, const uint8_t *from, uint8_t *to,
    const uint8_t iv[AES_BLOCK_LEN])
{

	aesni_crypt_xts(rounds, data_schedule, tweak_schedule, len, from, to,
	    iv, 0);
}
