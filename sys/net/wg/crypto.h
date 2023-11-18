/* SPDX-License-Identifier: MIT
 *
 * Copyright (C) 2015-2021 Jason A. Donenfeld <Jason@zx2c4.com>. All Rights Reserved.
 * Copyright (c) 2022 The FreeBSD Foundation
 */

#ifndef _WG_CRYPTO
#define _WG_CRYPTO

#include <sys/param.h>
#include <sys/endian.h>

#include <crypto/chacha20_poly1305.h>

struct mbuf;

int crypto_init(void);
void crypto_deinit(void);

enum chacha20poly1305_lengths {
	XCHACHA20POLY1305_NONCE_SIZE = 24,
	CHACHA20POLY1305_KEY_SIZE = 32,
	CHACHA20POLY1305_AUTHTAG_SIZE = 16
};

static inline void
chacha20poly1305_encrypt(uint8_t *dst, const uint8_t *src, const size_t src_len,
			 const uint8_t *ad, const size_t ad_len,
			 const uint64_t nonce,
			 const uint8_t key[CHACHA20POLY1305_KEY_SIZE])
{
	uint8_t nonce_bytes[8];

	le64enc(nonce_bytes, nonce);
	chacha20_poly1305_encrypt(dst, src, src_len, ad, ad_len,
				  nonce_bytes, sizeof(nonce_bytes), key);
}

static inline bool
chacha20poly1305_decrypt(uint8_t *dst, const uint8_t *src, const size_t src_len,
			 const uint8_t *ad, const size_t ad_len,
			 const uint64_t nonce,
			 const uint8_t key[CHACHA20POLY1305_KEY_SIZE])
{
	uint8_t nonce_bytes[8];

	le64enc(nonce_bytes, nonce);
	return (chacha20_poly1305_decrypt(dst, src, src_len, ad, ad_len,
					  nonce_bytes, sizeof(nonce_bytes), key));
}

static inline void
xchacha20poly1305_encrypt(uint8_t *dst, const uint8_t *src,
			  const size_t src_len, const uint8_t *ad,
			  const size_t ad_len,
			  const uint8_t nonce[XCHACHA20POLY1305_NONCE_SIZE],
			  const uint8_t key[CHACHA20POLY1305_KEY_SIZE])
{
	xchacha20_poly1305_encrypt(dst, src, src_len, ad, ad_len, nonce, key);
}

static inline bool
xchacha20poly1305_decrypt(uint8_t *dst, const uint8_t *src,
			  const size_t src_len,  const uint8_t *ad,
			  const size_t ad_len,
			  const uint8_t nonce[XCHACHA20POLY1305_NONCE_SIZE],
			  const uint8_t key[CHACHA20POLY1305_KEY_SIZE])
{
	return (xchacha20_poly1305_decrypt(dst, src, src_len, ad, ad_len,
					   nonce, key));
}

int chacha20poly1305_encrypt_mbuf(struct mbuf *, const uint64_t nonce,
				  const uint8_t key[CHACHA20POLY1305_KEY_SIZE]);
int chacha20poly1305_decrypt_mbuf(struct mbuf *, const uint64_t nonce,
				  const uint8_t key[CHACHA20POLY1305_KEY_SIZE]);


#endif
