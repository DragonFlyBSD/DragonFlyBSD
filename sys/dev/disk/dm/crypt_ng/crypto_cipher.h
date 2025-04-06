/*
 * Copyright (c) 2025 The DragonFly Project.  All rights reserved.
 *
 * This code is derived from software contributed to The DragonFly Project
 * by Michael Neumann <mneumann@ntecs.de>.
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

#ifndef _CRYPTO_CIPHER_H_
#define _CRYPTO_CIPHER_H_

#include <crypto/aesni/aesni.h>
#include <crypto/rijndael/rijndael.h>

struct aes_xts_ctx {
	rijndael_ctx key1;
	rijndael_ctx key2;
};

typedef struct {
	uint8_t iv[AES_BLOCK_LEN];
} aesni_iv;

struct aesni_key_schedules {
	uint8_t enc_schedule[AES_SCHED_LEN] __aligned(16);
	uint8_t dec_schedule[AES_SCHED_LEN] __aligned(16);
	uint8_t xts_schedule[AES_SCHED_LEN] __aligned(16);
};

struct aesni_key_schedules_padded {
	struct aesni_key_schedules schedules;
	uint8_t _padding[AESNI_ALIGN];
};

typedef struct {
	struct aesni_key_schedules_padded key_schedules;
	int rounds;
} aesni_ctx;

struct crypto_cipher_context {
	union {
		rijndael_ctx _rijndael;
		aesni_ctx _aesni;
		struct aes_xts_ctx _aes_xts;
	} _ctx;
};

struct crypto_cipher_iv {
	union {
		uint8_t _rijndael[16];
		aesni_iv _aesni;
		uint8_t _aes_xts[16]; /* 16 bytes are used, but the last 8 bytes
					 are zero */
	} _iv;
};

typedef int (*crypto_cipher_blockfn_t)(const struct crypto_cipher_context *ctx,
    uint8_t *data, int datalen, struct crypto_cipher_iv *iv);

typedef int (*crypto_cipher_probe_t)(const char *algo_name,
    const char *mode_name, int keysize_in_bits);

typedef int (*crypto_cipher_setkey_t)(struct crypto_cipher_context *ctx,
    const uint8_t *keydata, int keylen_in_bytes);

struct crypto_cipher {
	const char *shortname;
	const char *description;
	uint16_t blocksize;
	uint16_t ivsize;
	uint16_t ctxsize;

	crypto_cipher_probe_t probe;
	crypto_cipher_setkey_t setkey;
	crypto_cipher_blockfn_t encrypt;
	crypto_cipher_blockfn_t decrypt;
};

const struct crypto_cipher *crypto_cipher_find(const char *algo_name,
    const char *mode_name, int keysize_in_bits);

#endif
