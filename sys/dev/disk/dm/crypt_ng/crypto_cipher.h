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

struct crypto_cipher_iv {
	union {
		uint8_t _rijndael[16];
		uint8_t _aesni[16];
		uint8_t _aes_xts[16]; /* 16 bytes are used, but the last 8 bytes
					 are zero */
	} _iv;
};

typedef int crypto_cipher_mode;

#define CRYPTO_CIPHER_ENCRYPT 0
#define CRYPTO_CIPHER_DECRYPT 1

struct crypto_cipher_spec;

typedef const struct crypto_cipher_spec *crypto_cipher_t;

typedef struct {
	crypto_cipher_t cipher;
	void *context;
	void *origptr;
} crypto_cipher_session_t;

typedef struct crypto_cipher_context *crypto_cipher_context_t;

crypto_cipher_t crypto_cipher_find(const char *algo_name, const char *mode_name,
    int keysize_in_bits);

const char *crypto_cipher_get_description(crypto_cipher_t cipher);

int crypto_cipher_initsession(crypto_cipher_t cipher,
    crypto_cipher_session_t *session);

int crypto_cipher_freesession(crypto_cipher_session_t *session);

int crypto_cipher_setkey(crypto_cipher_session_t *session,
    const uint8_t *keydata, int keylen_in_bytes);

int crypto_cipher_encrypt(const crypto_cipher_session_t *session, uint8_t *data,
    int datalen, struct crypto_cipher_iv *iv);

int crypto_cipher_decrypt(const crypto_cipher_session_t *session, uint8_t *data,
    int datalen, struct crypto_cipher_iv *iv);

int crypto_cipher_crypt(const crypto_cipher_session_t *session, uint8_t *data,
    int datalen, struct crypto_cipher_iv *iv, crypto_cipher_mode mode);

#endif
