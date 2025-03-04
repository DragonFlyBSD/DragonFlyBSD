/*	$OpenBSD: gmac.c,v 1.3 2011/01/11 15:44:23 deraadt Exp $	*/

/*
 * Copyright (c) 2010 Mike Belopuhov <mike@vantronix.net>
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

/*
 * This code implements the Message Authentication part of the
 * Galois/Counter Mode (as being described in the RFC 4543) using
 * the AES cipher.  FIPS SP 800-38D describes the algorithm details.
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/endian.h>

#include <crypto/rijndael/rijndael.h>
#include <opencrypto/gmac.h>

void	ghash_gfmul(uint32_t *, uint32_t *, uint32_t *);
void	ghash_update(GHASH_CTX *, uint8_t *, size_t);

/* Computes a block multiplication in the GF(2^128) */
void
ghash_gfmul(uint32_t *X, uint32_t *Y, uint32_t *product)
{
	uint32_t	v[4];
	uint32_t	z[4] = { 0, 0, 0, 0};
	uint8_t		*x = (uint8_t *)X;
	uint32_t	mul;
	int		i;

	v[0] = be32toh(Y[0]);
	v[1] = be32toh(Y[1]);
	v[2] = be32toh(Y[2]);
	v[3] = be32toh(Y[3]);

	for (i = 0; i < GMAC_BLOCK_LEN * 8; i++) {
		/* update Z */
		if (x[i >> 3] & (1 << (~i & 7))) {
			z[0] ^= v[0];
			z[1] ^= v[1];
			z[2] ^= v[2];
			z[3] ^= v[3];
		} /* else: we preserve old values */

		/* update V */
		mul = v[3] & 1;
		v[3] = (v[2] << 31) | (v[3] >> 1);
		v[2] = (v[1] << 31) | (v[2] >> 1);
		v[1] = (v[0] << 31) | (v[1] >> 1);
		v[0] = (v[0] >> 1) ^ (0xe1000000 * mul);
	}

	product[0] = htobe32(z[0]);
	product[1] = htobe32(z[1]);
	product[2] = htobe32(z[2]);
	product[3] = htobe32(z[3]);
}

void
ghash_update(GHASH_CTX *ctx, uint8_t *X, size_t len)
{
	uint32_t	*x = (uint32_t *)X;
	uint32_t	*s = (uint32_t *)ctx->S;
	uint32_t	*y = (uint32_t *)ctx->Z;
	int		i;

	for (i = 0; i < len / GMAC_BLOCK_LEN; i++) {
		s[0] = y[0] ^ x[0];
		s[1] = y[1] ^ x[1];
		s[2] = y[2] ^ x[2];
		s[3] = y[3] ^ x[3];

		ghash_gfmul((uint32_t *)ctx->S, (uint32_t *)ctx->H,
		    (uint32_t *)ctx->S);

		y = s;
		x += 4;
	}

	bcopy(ctx->S, ctx->Z, GMAC_BLOCK_LEN);
}

#define AESCTR_NONCESIZE	4

void
AES_GMAC_Init(AES_GMAC_CTX *ctx)
{
	bzero(ctx->ghash.H, GMAC_BLOCK_LEN);
	bzero(ctx->ghash.S, GMAC_BLOCK_LEN);
	bzero(ctx->ghash.Z, GMAC_BLOCK_LEN);
	bzero(ctx->J, GMAC_BLOCK_LEN);
}

int
AES_GMAC_Setkey(AES_GMAC_CTX *ctx, const uint8_t *key, uint16_t klen)
{
	ctx->rounds = rijndaelKeySetupEnc(ctx->K, __DECONST(u_char *, key),
	    (klen - AESCTR_NONCESIZE) * 8);
	/* copy out salt to the counter block */
	bcopy(key + klen - AESCTR_NONCESIZE, ctx->J, AESCTR_NONCESIZE);
	/* prepare a hash subkey */
	rijndaelEncrypt(ctx->K, ctx->rounds, ctx->ghash.H, ctx->ghash.H);
	return (0);
}

void
AES_GMAC_Reinit(AES_GMAC_CTX *ctx, const uint8_t *iv, uint16_t ivlen)
{
	/* copy out IV to the counter block */
	bcopy(iv, ctx->J + AESCTR_NONCESIZE, ivlen);
}

int
AES_GMAC_Update(AES_GMAC_CTX *ctx, uint8_t *data, uint16_t len)
{
	uint32_t	blk[4] = { 0, 0, 0, 0 };
	int		plen;

	if (len > 0) {
		plen = len % GMAC_BLOCK_LEN;
		if (len >= GMAC_BLOCK_LEN)
			ghash_update(&ctx->ghash, data, len - plen);
		if (plen) {
			bcopy(data + (len - plen), (uint8_t *)blk, plen);
			ghash_update(&ctx->ghash, (uint8_t *)blk,
			    GMAC_BLOCK_LEN);
		}
	}
	return (0);
}

void
AES_GMAC_Final(uint8_t digest[GMAC_DIGEST_LEN], AES_GMAC_CTX *ctx)
{
	uint8_t		keystream[GMAC_BLOCK_LEN];
	int		i;

	/* do one round of GCTR */
	ctx->J[GMAC_BLOCK_LEN - 1] = 1;
	rijndaelEncrypt(ctx->K, ctx->rounds, ctx->J, keystream);
	for (i = 0; i < GMAC_DIGEST_LEN; i++)
		digest[i] = ctx->ghash.S[i] ^ keystream[i];
	bzero(keystream, sizeof(keystream));
}

