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


struct chacha20poly1305_ctx {
	struct chacha_ctx	chacha;
	poly1305_state		poly;
	uint8_t			tag[CHACHA20POLY1305_AUTHTAG_SIZE];
	size_t			ad_len;
	size_t			data_len;
	int			flags;
#define F_MODE_ENCRYPTION	0x001	/* encryption operation */
#define F_INITIALIZED		0x002	/* context initialized */
#define F_AD_DONE		0x004	/* no more additional data */
};

#define PADDING_SIZE		16
#define PADDING_LEN(len) \
	((PADDING_SIZE - ((len) & (PADDING_SIZE - 1))) & (PADDING_SIZE - 1))

static const uint8_t pad0[PADDING_SIZE] = { 0 };


static void
_chacha20poly1305_init(struct chacha20poly1305_ctx *ctx, bool enc_mode,
		       const uint8_t nonce[CHACHA20POLY1305_NONCE_SIZE],
		       const uint8_t key[CHACHA20POLY1305_KEY_SIZE])
{
	uint8_t poly_key[CHACHA20POLY1305_KEY_SIZE];
	uint8_t nonce64[CHACHA_NONCELEN], counter64[CHACHA_CTRLEN];

	bzero(ctx, sizeof(*ctx));
	bzero(poly_key, sizeof(poly_key));
	bzero(nonce64, sizeof(nonce64));
	bzero(counter64, sizeof(counter64));

	/*
	 * The original ChaCha uses a 64-bit nonce and a 64-bit counter,
	 * but the IETF version is modified to use a 96-bit nonce and
	 * a 32-bit counter. (RFC 8439)
	 */
	memcpy(counter64 + 4, nonce, 4);
	memcpy(nonce64, nonce + 4, 8);

	/* Generate the Poly1305 one-time key. */
	chacha_keysetup(&ctx->chacha, key, CHACHA20POLY1305_KEY_SIZE * 8);
	chacha_ivsetup(&ctx->chacha, nonce64, counter64);
	chacha_encrypt_bytes(&ctx->chacha, poly_key, poly_key,
			     sizeof(poly_key));

	/* Initialize the authenticator. */
	poly1305_init(&ctx->poly, poly_key);

	ctx->flags |= (enc_mode ? F_MODE_ENCRYPTION : 0);
	ctx->flags |= F_INITIALIZED;
}

/*
 * Process the AD (additional data) and cipher data (i.e., plaintext or
 * ciphertext).
 *
 * If the output buffer <out> is NULL, then the input data are regarded
 * as AAD.  The AAD can be provided in multiple pieces, but must be done
 * before any cipher data.
 *
 * NOTE: The cipher data must be complete blocks.  This requirement is
 *       placed to help easily implement the in-place encryption/
 *       decryption for data in non-contiguous buffers (e.g., mbuf chain).
 */
static void
_chacha20poly1305_update(struct chacha20poly1305_ctx *ctx,
			 uint8_t *out, const uint8_t *in, size_t in_len)
{
	KKASSERT((ctx->flags & F_INITIALIZED) != 0);

	if (out == NULL) {
		/* Additional data */
		KKASSERT((ctx->flags & F_AD_DONE) == 0);
		poly1305_update(&ctx->poly, in, in_len);
		ctx->ad_len += in_len;
		return;
	}

	/* Cipher data: must be complete blocks. */
	KKASSERT(in_len % CHACHA_BLOCKLEN == 0);

	if ((ctx->flags & F_AD_DONE) == 0) {
		poly1305_update(&ctx->poly, pad0, PADDING_LEN(ctx->ad_len));
		ctx->flags |= F_AD_DONE;
	}

	/* Swap the Poly1305/ChaCha order to support in-place operation. */
	if (ctx->flags & F_MODE_ENCRYPTION) {
		chacha_encrypt_bytes(&ctx->chacha, in, out, in_len);
		poly1305_update(&ctx->poly, out, in_len);
	} else {
		poly1305_update(&ctx->poly, in, in_len);
		chacha_encrypt_bytes(&ctx->chacha, in, out, in_len);
	}

	ctx->data_len += in_len;
}

/*
 * Process the last cipher block, which can be empty, complete or incomplete.
 *
 * In the encryption mode, the tag is calculated and is available via
 * 'ctx->tag'.  Let the caller get the tag and append to the output, because
 * the allocated tag space may not directly follow the output buffer; e.g.,
 * the tag is allocated on a separate mbuf.
 *
 * In the decryption mode, the caller must set the expected tag in
 * 'ctx->tag' before calling this function.  The tag will be verified and
 * a boolean is returned to indicate whether the tag matches.
 */
static bool
_chacha20poly1305_final(struct chacha20poly1305_ctx *ctx,
			uint8_t *out, const uint8_t *in, size_t in_len)
{
	uint64_t lens[2];
	uint8_t tag[CHACHA20POLY1305_AUTHTAG_SIZE];
	bool ret;

	KKASSERT((ctx->flags & F_INITIALIZED) != 0);
	KKASSERT(in_len <= CHACHA_BLOCKLEN);
	KKASSERT(out != NULL);

	if (ctx->ad_len > 0 && (ctx->flags & F_AD_DONE) == 0) {
		KKASSERT(ctx->data_len == 0);
		poly1305_update(&ctx->poly, pad0, PADDING_LEN(ctx->ad_len));
		ctx->flags |= F_AD_DONE;
	}

	if (ctx->flags & F_MODE_ENCRYPTION) {
		chacha_encrypt_bytes(&ctx->chacha, in, out, in_len);
		poly1305_update(&ctx->poly, out, in_len);
	} else {
		poly1305_update(&ctx->poly, in, in_len);
		chacha_encrypt_bytes(&ctx->chacha, in, out, in_len);
	}
	poly1305_update(&ctx->poly, pad0, PADDING_LEN(in_len));

	ctx->data_len += in_len;

	/* Calculate the tag. */
	lens[0] = htole64(ctx->ad_len);
	lens[1] = htole64(ctx->data_len);
	poly1305_update(&ctx->poly, (uint8_t *)lens, sizeof(lens));
	poly1305_finish(&ctx->poly, tag);

	if (ctx->flags & F_MODE_ENCRYPTION) {
		ret = true;
		explicit_bzero(ctx, sizeof(*ctx));
		memcpy(ctx->tag, tag, sizeof(tag));
	} else {
		ret = (timingsafe_bcmp(ctx->tag, tag, sizeof(tag)) == 0);
		explicit_bzero(ctx, sizeof(*ctx));
	}

	return (ret);
}

/*-------------------------------------------------------------------*/

void
chacha20poly1305_encrypt(uint8_t *dst, const uint8_t *src, size_t src_len,
			 const uint8_t *ad, size_t ad_len,
			 const uint8_t nonce[CHACHA20POLY1305_NONCE_SIZE],
			 const uint8_t key[CHACHA20POLY1305_KEY_SIZE])
{
	struct chacha20poly1305_ctx ctx;
	size_t len;

	_chacha20poly1305_init(&ctx, true, nonce, key);

	_chacha20poly1305_update(&ctx, NULL, ad, ad_len);

	len = rounddown2(src_len, CHACHA_BLOCKLEN);
	_chacha20poly1305_update(&ctx, dst, src, len);

	_chacha20poly1305_final(&ctx, dst + len, src + len, src_len - len);
	memcpy(dst + src_len, ctx.tag, sizeof(ctx.tag));
}

bool
chacha20poly1305_decrypt(uint8_t *dst, const uint8_t *src, size_t src_len,
			 const uint8_t *ad, size_t ad_len,
			 const uint8_t nonce[CHACHA20POLY1305_NONCE_SIZE],
			 const uint8_t key[CHACHA20POLY1305_KEY_SIZE])
{
	struct chacha20poly1305_ctx ctx;
	size_t data_len, len;

	if (src_len < sizeof(ctx.tag))
		return (false);

	_chacha20poly1305_init(&ctx, false, nonce, key);

	_chacha20poly1305_update(&ctx, NULL, ad, ad_len);

	data_len = src_len - sizeof(ctx.tag);
	len = rounddown2(data_len, CHACHA_BLOCKLEN);
	_chacha20poly1305_update(&ctx, dst, src, len);

	memcpy(ctx.tag, src + data_len, sizeof(ctx.tag));
	return _chacha20poly1305_final(&ctx, dst + len, src + len,
				       data_len - len);
}

/*-------------------------------------------------------------------*/

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
