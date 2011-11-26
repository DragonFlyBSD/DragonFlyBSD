/*
 * Copyright (C) 2008, Damien Miller
 * Copyright (C) 2011, Alex Hornung
 *
 * Permission to use, copy, and modify this software with or without fee
 * is hereby granted, provided that this entire notice is included in
 * all copies of any software which is or includes a copy or
 * modification of this software.
 * You may use this code under the GNU public license if you so wish. Please
 * contribute changes back to the authors under this freer than GPL license
 * so that we may further the use of strong encryption without limitations to
 * all.
 *
 * THIS SOFTWARE IS BEING PROVIDED "AS IS", WITHOUT ANY EXPRESS OR
 * IMPLIED WARRANTY. IN PARTICULAR, NONE OF THE AUTHORS MAKES ANY
 * REPRESENTATION OR WARRANTY OF ANY KIND CONCERNING THE
 * MERCHANTABILITY OF THIS SOFTWARE OR ITS FITNESS FOR ANY PARTICULAR
 * PURPOSE.
 */

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <inttypes.h>

#include "tcplay.h"
#include "generic_xts.h"



static int
xts_reinit(struct xts_ctx *ctx, u_int64_t blocknum)
{
	u_int i;

	/*
	 * Prepare tweak as E_k2(IV). IV is specified as LE representation
	 * of a 64-bit block number which we allow to be passed in directly.
	 */
	for (i = 0; i < XTS_IVSIZE; i++) {
		ctx->tweak[i] = blocknum & 0xff;
		blocknum >>= 8;
	}
	/* Last 64 bits of IV are always zero */
	bzero(ctx->tweak + XTS_IVSIZE, XTS_IVSIZE);

	return ctx->encrypt_fn(ctx->ctx2, ctx->blk_sz, ctx->tweak, ctx->tweak);
}

static int
xts_crypt(struct xts_ctx *ctx, u_int8_t *data, u_int do_encrypt)
{
	u_int8_t block[XTS_MAX_BLOCKSIZE];
	u_int i, carry_in, carry_out;
	int err;

	for (i = 0; i < ctx->blk_sz; i++)
		block[i] = data[i] ^ ctx->tweak[i];

	if (do_encrypt)
		err = ctx->encrypt_fn(ctx->ctx1, ctx->blk_sz, block, data);
	else
		err = ctx->decrypt_fn(ctx->ctx1, ctx->blk_sz, block, data);

	if (err)
		goto out;

	for (i = 0; i < ctx->blk_sz; i++)
		data[i] ^= ctx->tweak[i];

	/* Exponentiate tweak */
	carry_in = 0;
	for (i = 0; i < ctx->blk_sz; i++) {
		carry_out = ctx->tweak[i] & 0x80;
		ctx->tweak[i] = (ctx->tweak[i] << 1) | (carry_in ? 1 : 0);
		carry_in = carry_out;
	}
	if (carry_in)
		ctx->tweak[0] ^= XTS_ALPHA;

out:
	bzero(block, sizeof(block));
	return err;
}

int
xts_init(struct xts_ctx *ctx, void *arg1, void *arg2, set_key_fn _set_key_fn,
    zero_key_fn _zero_key_fn, encrypt_decrypt_fn _encrypt_fn,
    encrypt_decrypt_fn _decrypt_fn, u_int blk_sz, u_int8_t *key, int len)
{
	int err;

	if (len != 32 && len != 64)
		return -1;

	ctx->blk_sz = blk_sz;
	ctx->encrypt_fn = _encrypt_fn;
	ctx->decrypt_fn = _decrypt_fn;
	ctx->set_key_fn = _set_key_fn;
	ctx->zero_key_fn = _zero_key_fn;

	err = ctx->set_key_fn(&ctx->ctx1, arg1, arg2, key, len * 4);
	if (err)
		return -1;

	err = ctx->set_key_fn(&ctx->ctx2, arg1, arg2, key + (len / 2),
	    len * 4);
	if (err) {
		ctx->zero_key_fn(&ctx->ctx1);
		return -1;
	}

	return 0;
}

int
xts_encrypt(struct xts_ctx *ctx, u_int8_t *data, size_t len, uint8_t *iv)
{
	uint64_t sector = *((uint64_t *)iv);
	int err;

	if ((len % ctx->blk_sz) != 0)
		return -1;

	err = xts_reinit(ctx, sector);
	if (err)
		return err;

	while (len > 0) {
		err = xts_crypt(ctx, data, 1);
		if (err)
			return -1;

		data += ctx->blk_sz;
		len -= ctx->blk_sz;
	}

	return err;
}

int
xts_decrypt(struct xts_ctx *ctx, u_int8_t *data, size_t len, uint8_t *iv)
{
	uint64_t sector = *((uint64_t *)iv);
	int err;

	if ((len % ctx->blk_sz) != 0)
		return -1;

	err = xts_reinit(ctx, sector);
	if (err)
		return err;

	while (len > 0) {
		err = xts_crypt(ctx, data, 0);
		if (err)
			return -1;

		data += ctx->blk_sz;
		len -= ctx->blk_sz;
	}

	return err;
}

int
xts_uninit(struct xts_ctx *ctx)
{
	ctx->zero_key_fn(&ctx->ctx1);
	ctx->zero_key_fn(&ctx->ctx2);

	return 0;
}

