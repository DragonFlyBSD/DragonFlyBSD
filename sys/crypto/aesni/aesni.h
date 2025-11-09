/*-
 * Copyright (c) 2010 Konstantin Belousov <kib@FreeBSD.org>
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
 * $FreeBSD: src/sys/crypto/aesni/aesni.h,v 1.2 2010/09/23 11:57:25 pjd Exp $
 */

#ifndef _CRYPTO_AESNI_H_
#define _CRYPTO_AESNI_H_

#include <sys/types.h>
#include <sys/_null.h>

#ifdef AES_BLOCK_LEN
#error "AES_BLOCK_LEN already defined!"
#else
#define AES_BLOCK_LEN	16
#endif

#define AESNI_ALIGN	16
#define	AES128_ROUNDS	10
#define	AES192_ROUNDS	12
#define	AES256_ROUNDS	14
#define	AES_SCHED_LEN	((AES256_ROUNDS + 1) * AES_BLOCK_LEN)

/*
 * Internal functions, implemented in assembler.
 */
void aesni_enc(int rounds, const uint8_t *key_schedule,
    const uint8_t from[AES_BLOCK_LEN], uint8_t to[AES_BLOCK_LEN],
    const uint8_t iv[AES_BLOCK_LEN]);
void aesni_dec(int rounds, const uint8_t *key_schedule,
    const uint8_t from[AES_BLOCK_LEN], uint8_t to[AES_BLOCK_LEN],
    const uint8_t iv[AES_BLOCK_LEN]);
void aesni_set_enckey(const uint8_t *userkey, uint8_t *encrypt_schedule,
    int number_of_rounds);
void aesni_set_deckey(const uint8_t *encrypt_schedule,
    uint8_t *decrypt_schedule, int number_of_rounds);

/*
 * Slightly more public interfaces.
 */
void aesni_encrypt_cbc(int rounds, const void *key_schedule, size_t len,
    const uint8_t *from, uint8_t *to, const uint8_t iv[AES_BLOCK_LEN]);
void aesni_decrypt_ecb(int rounds, const void *key_schedule, size_t len,
    const uint8_t from[AES_BLOCK_LEN], uint8_t to[AES_BLOCK_LEN]);
void aesni_encrypt_ecb(int rounds, const void *key_schedule, size_t len,
    const uint8_t from[AES_BLOCK_LEN], uint8_t to[AES_BLOCK_LEN]);
void aesni_decrypt_cbc(int rounds, const void *key_schedule, size_t len,
    uint8_t *data, const uint8_t iv[AES_BLOCK_LEN]);

void
aesni_crypt_xts_block(int rounds, const void *key_schedule, uint8_t *tweak,
    const uint8_t *from, uint8_t *to, int do_encrypt);

void
aesni_crypt_xts(int rounds, const void *data_schedule,
    const void *tweak_schedule, size_t len, const uint8_t *from, uint8_t *to,
    const uint8_t iv[AES_BLOCK_LEN], int do_encrypt);

void
aesni_encrypt_xts(int rounds, const void *data_schedule,
    const void *tweak_schedule, size_t len, const uint8_t *from, uint8_t *to,
    const uint8_t iv[AES_BLOCK_LEN]);

void
aesni_decrypt_xts(int rounds, const void *data_schedule,
    const void *tweak_schedule, size_t len, const uint8_t *from, uint8_t *to,
    const uint8_t iv[AES_BLOCK_LEN]);

#endif
