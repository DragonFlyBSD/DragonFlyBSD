/*
 * Copyright (c) 2015 Mike Belopuhov
 * Copyright (c) 2023 Aaron LI <aly@aaronly.me>
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/endian.h>

#include <crypto/chachapoly.h>
#include <crypto/chacha20/chacha.h>
#include <crypto/poly1305/poly1305.h>


#define PADDING_SIZE		16
#define PADDING_LEN(len) \
	((PADDING_SIZE - ((len) & (PADDING_SIZE - 1))) & (PADDING_SIZE - 1))

static const uint8_t pad0[PADDING_SIZE] = { 0 };


void
chacha20poly1305_encrypt(uint8_t *dst, const uint8_t *src, size_t src_len,
			 const uint8_t *ad, size_t ad_len,
			 const uint8_t nonce[CHACHA20POLY1305_NONCE_SIZE],
			 const uint8_t key[CHACHA20POLY1305_KEY_SIZE])
{
	struct chacha_ctx chacha;
	poly1305_state poly;
	uint64_t lens[2];
	uint8_t poly_key[CHACHA20POLY1305_KEY_SIZE];
	uint8_t nonce64[CHACHA_NONCELEN], counter64[CHACHA_CTRLEN];

	bzero(poly_key, sizeof(poly_key));
	bzero(nonce64, sizeof(nonce64));
	bzero(counter64, sizeof(counter64));

	/*
	 * The original ChaCha uses a 64-bit nonce and a 64-bit counter,
	 * while the IETF version modified it to be a 96-bit nonce and
	 * a 32-bit counter. (RFC 8439)
	 */
	memcpy(counter64 + 4, nonce, 4);
	memcpy(nonce64, nonce + 4, 8);

	/* Generate the Poly1305 one-time key. */
	chacha_keysetup(&chacha, key, CHACHA20POLY1305_KEY_SIZE * 8);
	chacha_ivsetup(&chacha, nonce64, counter64);
	chacha_encrypt_bytes(&chacha, poly_key, poly_key, sizeof(poly_key));

	/* Encrypt the plaintext. */
	chacha_encrypt_bytes(&chacha, src, dst, src_len);

	/* Calculate the tag. */
	poly1305_init(&poly, poly_key);
	poly1305_update(&poly, ad, ad_len);
	poly1305_update(&poly, pad0, PADDING_LEN(ad_len));
	poly1305_update(&poly, dst, src_len);
	poly1305_update(&poly, pad0, PADDING_LEN(src_len));
	lens[0] = htole64(ad_len);
	lens[1] = htole64(src_len);
	poly1305_update(&poly, (uint8_t *)lens, sizeof(lens));
	poly1305_finish(&poly, dst + src_len);

	explicit_bzero(&chacha, sizeof(chacha));
	explicit_bzero(poly_key, sizeof(poly_key));
}

bool
chacha20poly1305_decrypt(uint8_t *dst, const uint8_t *src, size_t src_len,
			 const uint8_t *ad, size_t ad_len,
			 const uint8_t nonce[CHACHA20POLY1305_NONCE_SIZE],
			 const uint8_t key[CHACHA20POLY1305_KEY_SIZE])
{
	struct chacha_ctx chacha;
	poly1305_state poly;
	uint64_t lens[2];
	uint8_t poly_key[CHACHA20POLY1305_KEY_SIZE];
	uint8_t tag[CHACHA20POLY1305_AUTHTAG_SIZE];
	uint8_t nonce64[CHACHA_NONCELEN], counter64[CHACHA_CTRLEN];
	size_t ct_len;
	int ret;

	if (src_len < CHACHA20POLY1305_AUTHTAG_SIZE)
		return (false);

	bzero(poly_key, sizeof(poly_key));
	bzero(nonce64, sizeof(nonce64));
	bzero(counter64, sizeof(counter64));

	memcpy(counter64 + 4, nonce, 4);
	memcpy(nonce64, nonce + 4, 8);

	/* Generate the Poly1305 one-time key. */
	chacha_keysetup(&chacha, key, CHACHA20POLY1305_KEY_SIZE * 8);
	chacha_ivsetup(&chacha, nonce64, counter64);
	chacha_encrypt_bytes(&chacha, poly_key, poly_key, sizeof(poly_key));

	/* Calculate the tag. */
	poly1305_init(&poly, poly_key);
	poly1305_update(&poly, ad, ad_len);
	poly1305_update(&poly, pad0, PADDING_LEN(ad_len));
	ct_len = src_len - CHACHA20POLY1305_AUTHTAG_SIZE;
	poly1305_update(&poly, src, ct_len);
	poly1305_update(&poly, pad0, PADDING_LEN(ct_len));
	lens[0] = htole64(ad_len);
	lens[1] = htole64(ct_len);
	poly1305_update(&poly, (uint8_t *)lens, sizeof(lens));
	poly1305_finish(&poly, tag);

	/* Compare the tag. */
	ret = timingsafe_bcmp(tag, src + ct_len, sizeof(tag));
	if (ret == 0) {
		/* Decrypt the ciphertext. */
		chacha_encrypt_bytes(&chacha, src, dst, ct_len);
	}

	explicit_bzero(&chacha, sizeof(chacha));
	explicit_bzero(poly_key, sizeof(poly_key));
	return (ret == 0);
}

/*
 * XChaCha: eXtended-nonce ChaCha and AEAD_XChaCha20_Poly1305
 * RFC draft: https://datatracker.ietf.org/doc/html/draft-irtf-cfrg-xchacha
 */
void
xchacha20poly1305_encrypt(uint8_t *dst, const uint8_t *src, size_t src_len,
			  const uint8_t *ad, size_t ad_len,
			  const uint8_t nonce[XCHACHA20POLY1305_NONCE_SIZE],
			  const uint8_t key[CHACHA20POLY1305_KEY_SIZE])
{
	uint8_t derived_key[CHACHA20POLY1305_KEY_SIZE];
	uint8_t derived_nonce[CHACHA20POLY1305_NONCE_SIZE];

	/* Derive a subkey using the first 16 bytes of the nonce. */
	hchacha20(derived_key, nonce, key);

	/* Prefix the remaining 8 bytes of the nonce with 4 NUL bytes. */
	bzero(derived_nonce, sizeof(derived_nonce));
	memcpy(derived_nonce + 4, nonce + 16, 8);

	chacha20poly1305_encrypt(dst, src, src_len, ad, ad_len,
				 derived_nonce, derived_key);

	explicit_bzero(derived_key, sizeof(derived_key));
}

bool
xchacha20poly1305_decrypt(uint8_t *dst, const uint8_t *src, size_t src_len,
			  const uint8_t *ad, size_t ad_len,
			  const uint8_t nonce[XCHACHA20POLY1305_NONCE_SIZE],
			  const uint8_t key[CHACHA20POLY1305_KEY_SIZE])
{
	uint8_t derived_key[CHACHA20POLY1305_KEY_SIZE];
	uint8_t derived_nonce[CHACHA20POLY1305_NONCE_SIZE];
	bool ret;

	hchacha20(derived_key, nonce, key);

	bzero(derived_nonce, sizeof(derived_nonce));
	memcpy(derived_nonce + 4, nonce + 16, 8);

	ret = chacha20poly1305_decrypt(dst, src, src_len, ad, ad_len,
				       derived_nonce, derived_key);

	explicit_bzero(derived_key, sizeof(derived_key));
	return (ret);
}
