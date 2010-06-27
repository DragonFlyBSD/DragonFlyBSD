/*
 * Copyright (c) 2010 The DragonFly Project.  All rights reserved.
 *
 * This code is derived from software contributed to The DragonFly Project
 * by Alex Hornung <ahornung@gmail.com>
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

/*
 * This file implements initial version of device-mapper crypt target.
 */
#include <sys/types.h>
#include <sys/param.h>

#include <sys/bio.h>
#include <sys/buf.h>
#include <sys/malloc.h>
#include <sys/vnode.h>
#include <crypto/sha1.h>
#include <crypto/sha2/sha2.h>
#include <opencrypto/cryptodev.h>

#include "dm.h"
MALLOC_DEFINE(M_DMCRYPT, "dm_crypt", "Device Mapper Target Crypt");

/*-
 * HMAC-SHA-224/256/384/512 implementation
 * Last update: 06/15/2005
 * Issue date:  06/15/2005
 *
 * Copyright (C) 2005 Olivier Gay <olivier.gay@a3.epfl.ch>
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
 * 3. Neither the name of the project nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE PROJECT AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE PROJECT OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

typedef struct {
    SHA512_CTX ctx_inside;
    SHA512_CTX ctx_outside;

    /* for hmac_reinit */
    SHA512_CTX ctx_inside_reinit;
    SHA512_CTX ctx_outside_reinit;

    unsigned char block_ipad[SHA512_BLOCK_LENGTH];
    unsigned char block_opad[SHA512_BLOCK_LENGTH];
} hmac_sha512_ctx;

/* HMAC-SHA-512 functions */
static void
hmac_sha512_init(hmac_sha512_ctx *ctx, unsigned char *key,
                      unsigned int key_size)
{
    unsigned int fill;
    unsigned int num;

    unsigned char *key_used;
    unsigned char key_temp[SHA512_DIGEST_LENGTH];
    int i;

    if (key_size == SHA512_BLOCK_LENGTH) {
        key_used = key;
        num = SHA512_BLOCK_LENGTH;
    } else {
        if (key_size > SHA512_BLOCK_LENGTH){
            key_used = key_temp;
            num = SHA512_DIGEST_LENGTH;
            SHA512_Data(key, key_size, key_used);
        } else { /* key_size > SHA512_BLOCK_LENGTH */
            key_used = key;
            num = key_size;
        }
        fill = SHA512_BLOCK_LENGTH - num;

        memset(ctx->block_ipad + num, 0x36, fill);
        memset(ctx->block_opad + num, 0x5c, fill);
    }

    for (i = 0; i < num; i++) {
        ctx->block_ipad[i] = key_used[i] ^ 0x36;
        ctx->block_opad[i] = key_used[i] ^ 0x5c;
    }

    SHA512_Init(&ctx->ctx_inside);
    SHA512_Update(&ctx->ctx_inside, ctx->block_ipad, SHA512_BLOCK_LENGTH);

    SHA512_Init(&ctx->ctx_outside);
    SHA512_Update(&ctx->ctx_outside, ctx->block_opad,
                  SHA512_BLOCK_LENGTH);

    /* for hmac_reinit */
    memcpy(&ctx->ctx_inside_reinit, &ctx->ctx_inside,
           sizeof(SHA512_CTX));
    memcpy(&ctx->ctx_outside_reinit, &ctx->ctx_outside,
           sizeof(SHA512_CTX));
}

#if 0
static void
hmac_sha512_reinit(hmac_sha512_ctx *ctx)
{
    memcpy(&ctx->ctx_inside, &ctx->ctx_inside_reinit,
           sizeof(SHA512_CTX));
    memcpy(&ctx->ctx_outside, &ctx->ctx_outside_reinit,
           sizeof(SHA512_CTX));
}
#endif

static void
hmac_sha512_update(hmac_sha512_ctx *ctx, unsigned char *message,
                        unsigned int message_len)
{
    SHA512_Update(&ctx->ctx_inside, message, message_len);
}

static void
hmac_sha512_final(hmac_sha512_ctx *ctx, unsigned char *mac,
                       unsigned int mac_size)
{
    unsigned char digest_inside[SHA512_DIGEST_LENGTH];
    unsigned char mac_temp[SHA512_DIGEST_LENGTH];

    SHA512_Final(digest_inside, &ctx->ctx_inside);
    SHA512_Update(&ctx->ctx_outside, digest_inside, SHA512_DIGEST_LENGTH);
    SHA512_Final(mac_temp, &ctx->ctx_outside);
    memcpy(mac, mac_temp, mac_size);
}

static void
hmac_sha512(unsigned char *key, unsigned int key_size,
          unsigned char *message, unsigned int message_len,
          unsigned char *mac, unsigned mac_size)
{
    hmac_sha512_ctx ctx;

    hmac_sha512_init(&ctx, key, key_size);
    hmac_sha512_update(&ctx, message, message_len);
    hmac_sha512_final(&ctx, mac, mac_size);
}

/*-
 * pkcs5_pbkdf2 function
 * Copyright (c) 2008 Damien Bergamini <damien.bergamini@free.fr>
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
 * Password-Based Key Derivation Function 2 (PKCS #5 v2.0).
 * Code based on IEEE Std 802.11-2007, Annex H.4.2.
 */
static int
pkcs5_pbkdf2(const char *pass, size_t pass_len, const char *salt, size_t salt_len,
    u_int8_t *key, size_t key_len, u_int rounds)
{
	u_int8_t *asalt, obuf[SHA512_DIGEST_LENGTH];
	u_int8_t d1[SHA512_DIGEST_LENGTH], d2[SHA512_DIGEST_LENGTH];
	u_int i, j;
	u_int count;
	size_t r;

	if (rounds < 1 || key_len == 0)
		return -1;
	if (salt_len == 0 || salt_len > SIZE_MAX - 1)
		return -1;
	if ((asalt = kmalloc(salt_len + 4, M_TEMP, M_WAITOK)) == NULL)
		return -1; /* XXX: this is not possible */

	memcpy(asalt, salt, salt_len);

	for (count = 1; key_len > 0; count++) {
		asalt[salt_len + 0] = (count >> 24) & 0xff;
		asalt[salt_len + 1] = (count >> 16) & 0xff;
		asalt[salt_len + 2] = (count >> 8) & 0xff;
		asalt[salt_len + 3] = count & 0xff;
		hmac_sha512(__DECONST(char *, pass), pass_len, asalt, salt_len + 4, d1, sizeof(d1));
		memcpy(obuf, d1, sizeof(obuf));

		for (i = 1; i < rounds; i++) {
			hmac_sha512(__DECONST(char *, pass), pass_len, d1, sizeof(d1), d2, sizeof(d2));
			memcpy(d1, d2, sizeof(d1));
			for (j = 0; j < sizeof(obuf); j++)
				obuf[j] ^= d1[j];
		}

		r = MIN(key_len, SHA512_DIGEST_LENGTH);
		memcpy(key, obuf, r);
		key += r;
		key_len -= r;
	};
	bzero(asalt, salt_len + 4);
	kfree(asalt, M_TEMP);
	bzero(d1, sizeof(d1));
	bzero(d2, sizeof(d2));
	bzero(obuf, sizeof(obuf));

	return 0;
}


/* ---------------------------------------------------------------------- */

struct target_crypt_config;
typedef void ivgen_t(struct target_crypt_config *, u_int8_t *, size_t, off_t);

typedef struct target_crypt_config {
	size_t	params_len;
	dm_pdev_t *pdev;
	int	crypto_alg;
	int	crypto_klen;
	u_int8_t	crypto_key[512>>3];
	u_int8_t	crypto_keyhash[SHA512_DIGEST_LENGTH];
	u_int64_t	crypto_sid;
	SHA512_CTX	essivsha512_ctx;
	struct cryptoini	crypto_session;
	ivgen_t	*crypto_ivgen;
	/* XXX: uuid */
} dm_target_crypt_config_t;

static void dm_target_crypt_work(dm_target_crypt_config_t *priv, struct bio *bio);
static void dm_target_crypt_read_done(struct bio *bio);
static void dm_target_crypt_write_done(struct bio *bio);
static int dm_target_crypt_crypto_done_read(struct cryptop *crp);
static int dm_target_crypt_crypto_done_write(struct cryptop *crp);


static void
essiv_hash_mkey(dm_target_crypt_config_t *priv)
{
	SHA1_CTX	ctxsha1;
	SHA256_CTX	ctx256;
	SHA384_CTX	ctx384;
	SHA512_CTX	ctx512;

	if (priv->crypto_klen <= 128) {
		SHA1Init(&ctxsha1);
		SHA1Update(&ctxsha1, priv->crypto_key, priv->crypto_klen>>3);
		SHA1Final(priv->crypto_keyhash, &ctxsha1);
	} else if (priv->crypto_klen <= 256) {
		SHA256_Init(&ctx256);
		SHA256_Update(&ctx256, priv->crypto_key, priv->crypto_klen>>3);
		SHA256_Final(priv->crypto_keyhash, &ctx256);
	} else if (priv->crypto_klen <= 384) {
		SHA384_Init(&ctx384);
		SHA384_Update(&ctx384, priv->crypto_key, priv->crypto_klen>>3);
		SHA384_Final(priv->crypto_keyhash, &ctx384);
	} else if (priv->crypto_klen <= 512) {
		SHA512_Init(&ctx512);
		SHA512_Update(&ctx512, priv->crypto_key, priv->crypto_klen>>3);
		SHA512_Final(priv->crypto_keyhash, &ctx512);
	} else {
		panic("Unexpected crypto_klen = %d", priv->crypto_klen);
	}
}

static int
essiv_ivgen_done(struct cryptop *crp)
{

	if (crp->crp_etype == EAGAIN)
		return crypto_dispatch(crp);

	if (crp->crp_etype != 0) {
		kprintf("essiv_ivgen_done, crp->crp_etype = %d\n", crp->crp_etype);
	}

	atomic_add_int((int *)crp->crp_opaque, 1);
	wakeup(crp->crp_opaque);
	return 0;
}

static void
essiv_ivgen(dm_target_crypt_config_t *priv, u_int8_t *iv, size_t iv_len, off_t sector)
{
	struct cryptodesc crd;
	struct cryptop crp;
	int error, id;

	id = 0;
	bzero(iv, iv_len);
	*((off_t *)iv) = sector;
	crp.crp_buf = (caddr_t)iv;

	crp.crp_sid = priv->crypto_sid;
	crp.crp_ilen = crp.crp_olen = iv_len;

	crp.crp_opaque = (void *)&id;

	crp.crp_callback = essiv_ivgen_done;

	crp.crp_desc = &crd;
	crp.crp_etype = 0;
	crp.crp_flags = CRYPTO_F_CBIFSYNC | CRYPTO_F_REL;

	crd.crd_alg = priv->crypto_alg;
	crd.crd_key = (caddr_t)priv->crypto_keyhash;
	crd.crd_klen = priv->crypto_klen;

	bzero(crd.crd_iv, sizeof(crd.crd_iv));

	crd.crd_skip = 0;
	crd.crd_len = iv_len;
	crd.crd_flags = CRD_F_KEY_EXPLICIT | CRD_F_IV_EXPLICIT | CRD_F_IV_PRESENT;
	crd.crd_flags |= CRD_F_ENCRYPT;
	crd.crd_next = NULL;

	error = crypto_dispatch(&crp);
	if (error)
		kprintf("essiv_ivgen, error = %d\n", error);

	/*
	 * id is modified in the callback, so that if crypto_dispatch finishes
	 * synchronously we don't tsleep() forever.
	 */
	if (id == 0)
		tsleep((void *)&error, 0, "essivgen", 0);
}


static void
alt_ivgen(dm_target_crypt_config_t *priv, u_int8_t *iv, size_t iv_len, off_t sector)
{

	SHA512_CTX	ctx512;
	u_int8_t	md[SHA512_DIGEST_LENGTH]; /* Max. Digest Size */

	memcpy(&ctx512, &priv->essivsha512_ctx, sizeof(SHA512_CTX));
	SHA512_Update(&ctx512, (u_int8_t*)&sector, sizeof(off_t));
	SHA512_Final(md, &ctx512);

	memcpy(iv, md, iv_len);
}

struct dmtc_helper {
	caddr_t	free_addr;
	caddr_t	orig_buf;
};

static void
dm_target_crypt_work(dm_target_crypt_config_t *priv, struct bio *bio)
{
	struct dmtc_helper *dmtc;
	struct cryptodesc *crd;
	struct cryptop *crp;
	struct cryptoini *cri;

	int error, i, bytes, isector, sectors, write, sz;
	u_char *ptr, *space, *data;

	cri = &priv->crypto_session;

	write = (bio->bio_buf->b_cmd == BUF_CMD_WRITE) ? 1 : 0;
	bytes = bio->bio_buf->b_bcount; /* XXX: b_resid no good after reads... == 0 */
	isector = bio->bio_offset/DEV_BSIZE;	/* Initial sector */
	sectors = bytes/DEV_BSIZE;		/* Number of sectors affected by bio */
	sz = sectors * (sizeof(*crp) + sizeof(*crd));

	if (write) {
		space = kmalloc(sizeof(struct dmtc_helper) + sz + bytes, M_DMCRYPT, M_WAITOK);
		dmtc = (struct dmtc_helper *)space;
		dmtc->free_addr = space;
		dmtc->orig_buf = bio->bio_buf->b_data;
		space += sizeof(struct dmtc_helper);
		memcpy(space + sz, bio->bio_buf->b_data, bytes);
		bio->bio_caller_info2.ptr = dmtc;
		bio->bio_buf->b_data = data = space + sz;
	} else {
		space = kmalloc(sz, M_DMCRYPT, M_WAITOK);
		data = bio->bio_buf->b_data;
		bio->bio_caller_info2.ptr = space;
	}

	ptr = space;
	bio->bio_caller_info3.value = sectors;
	kprintf("Write? %d, bytes = %d (b_bcount), sectors = %d (bio = %p, b_cmd = %d)\n", write, bytes, sectors, bio, bio->bio_buf->b_cmd);

	for (i = 0; i < sectors; i++) {
		crp = (struct cryptop *)ptr;
		ptr += sizeof(*crp);
		crd = (struct cryptodesc *)ptr;
		ptr += sizeof (*crd);

		crp->crp_buf = (data + i*DEV_BSIZE);

		crp->crp_sid = priv->crypto_sid;
		crp->crp_ilen = crp->crp_olen = DEV_BSIZE;

		crp->crp_opaque = (void *)bio;

		if (write)
			crp->crp_callback = dm_target_crypt_crypto_done_write;
		else
			crp->crp_callback = dm_target_crypt_crypto_done_read;
		crp->crp_desc = crd;
		crp->crp_etype = 0;
		crp->crp_flags = CRYPTO_F_CBIFSYNC | CRYPTO_F_REL;

		crd->crd_alg = priv->crypto_alg;
		crd->crd_key = (caddr_t)priv->crypto_key;
		crd->crd_klen = priv->crypto_klen;
#if 0
		bzero(crd->crd_iv, EALG_MAX_BLOCK_LEN);
		alt_ivgen(priv, crd->crd_iv, sizeof(crd->crd_iv), isector + i);
		essiv_ivgen(priv, crd->crd_iv, sizeof(crd->crd_iv), isector + i);
#endif
		priv->crypto_ivgen(priv, crd->crd_iv, sizeof(crd->crd_iv), isector + i);

		crd->crd_skip = 0;
		crd->crd_len = DEV_BSIZE /* XXX */;
		crd->crd_flags = CRD_F_KEY_EXPLICIT | CRD_F_IV_EXPLICIT | CRD_F_IV_PRESENT;
		crd->crd_next = NULL;

		if (write)
			crd->crd_flags |= CRD_F_ENCRYPT;
		else
			crd->crd_flags &= ~CRD_F_ENCRYPT;

		error = crypto_dispatch(crp);
	}
}

static void
dm_target_crypt_read_done(struct bio *bio)
{
	dm_target_crypt_config_t *priv;

	priv = bio->bio_caller_info1.ptr;
	kprintf("dm_target_crypt_read_done %p\n", bio);

	dm_target_crypt_work(priv, bio);
}

static void
dm_target_crypt_write_done(struct bio *bio)
{
	struct dmtc_helper *dmtc;
	struct bio *obio;

	kprintf("dm_target_crypt_write_done %p\n", bio);
	dmtc = bio->bio_caller_info2.ptr;
	bio->bio_buf->b_data = dmtc->orig_buf;
	kfree(dmtc->free_addr, M_DMCRYPT);
	obio = pop_bio(bio);
	biodone(obio);
}

static int
dm_target_crypt_crypto_done_read(struct cryptop *crp)
{
	struct bio *bio, *obio;
	int n;

	if (crp->crp_etype == EAGAIN)
		return crypto_dispatch(crp);

	bio = (struct bio *)crp->crp_opaque;
	KKASSERT(bio != NULL);
	
	n = atomic_fetchadd_int(&bio->bio_caller_info3.value, -1);
#if 0
	kprintf("dm_target_crypt_crypto_done_read %p, n = %d\n", bio, n);
#endif
	if (crp->crp_etype != 0) {
		kprintf("dm_target_crypt_crypto_done_read crp_etype = %d\n", crp->crp_etype);
		/* XXX: Print something out */
		bio->bio_buf->b_error = crp->crp_etype;	
	}
	if (n == 1) {
		kprintf("dm_target_crypt_crypt_done_read: n == 1\n");
		kfree(bio->bio_caller_info2.ptr, M_DMCRYPT);
		/* This is the last chunk of the read */
		obio = pop_bio(bio);
		biodone(obio);
	}

	return 0;
}

static int
dm_target_crypt_crypto_done_write(struct cryptop *crp)
{
	struct dmtc_helper *dmtc;
	dm_target_crypt_config_t *priv;
	struct bio *bio, *obio;
	int n;

	if (crp->crp_etype == EAGAIN)
		return crypto_dispatch(crp);

	bio = (struct bio *)crp->crp_opaque;
	KKASSERT(bio != NULL);
	
	n = atomic_fetchadd_int(&bio->bio_caller_info3.value, -1);
#if 0
	kprintf("dm_target_crypt_crypto_done_write %p, n = %d\n", bio, n);
#endif
	if (crp->crp_etype != 0) {
		kprintf("dm_target_crypt_crypto_done_write crp_etype = %d\n", crp->crp_etype);
		/* XXX: Print something out */
		bio->bio_buf->b_error = crp->crp_etype;	
	}
	if (n == 1) {
		kprintf("dm_target_crypt_crypt_done_write: n == 1\n");
		/* This is the last chunk of the write */
		if (bio->bio_buf->b_error != 0) {
			/* XXX */
			dmtc = bio->bio_caller_info2.ptr;
			bio->bio_buf->b_data = dmtc->orig_buf;
			kfree(dmtc->free_addr, M_DMCRYPT);
			obio = pop_bio(bio);
			biodone(obio);
		} else {
			priv = (dm_target_crypt_config_t *)bio->bio_caller_info1.ptr;
			vn_strategy(priv->pdev->pdev_vnode, bio);
		}
	}

	return 0;
}

/*
strategy -> read_done -> crypto_work -> crypto_done_read -> FINISH READ
strategy -> crypto_work -> crypto_done_write -> write dispatch -> write_done -> FINISH WRITE
*/

#ifdef DM_TARGET_MODULE
/*
 * Every target can be compiled directly to dm driver or as a
 * separate module this part of target is used for loading targets
 * to dm driver.
 * Target can be unloaded from kernel only if there are no users of
 * it e.g. there are no devices which uses that target.
 */
#include <sys/kernel.h>
#include <sys/module.h>

static int
dm_target_crypt_modcmd(modcmd_t cmd, void *arg)
{
	dm_target_t *dmt;
	int r;
	dmt = NULL;

	switch (cmd) {
	case MODULE_CMD_INIT:
		if ((dmt = dm_target_lookup("crypt")) != NULL) {
			dm_target_unbusy(dmt);
			return EEXIST;
		}
		dmt = dm_target_alloc("crypt");

		dmt->version[0] = 1;
		dmt->version[1] = 0;
		dmt->version[2] = 0;
		strlcpy(dmt->name, "crypt", DM_MAX_TYPE_NAME);
		dmt->init = &dm_target_crypt_init;
		dmt->status = &dm_target_crypt_status;
		dmt->strategy = &dm_target_crypt_strategy;
		dmt->deps = &dm_target_crypt_deps;
		dmt->destroy = &dm_target_crypt_destroy;
		dmt->upcall = &dm_target_crypt_upcall;

		r = dm_target_insert(dmt);

		break;

	case MODULE_CMD_FINI:
		r = dm_target_rem("crypt");
		break;

	case MODULE_CMD_STAT:
		return ENOTTY;

	default:
		return ENOTTY;
	}

	return r;
}
#endif

/*
 * Init function called from dm_table_load_ioctl.
 * Accepted format:
 * <device>	<crypto algorithm>[-<keysize>]	<iv generator>	<passphrase>
 * /dev/foo	aes-256				essiv		foobar
 */
int
dm_target_crypt_init(dm_dev_t * dmv, void **target_config, char *params)
{
	dm_target_crypt_config_t *priv;
	size_t len;
	char **ap, *args[5];
	int argc, klen, error;

	if (params == NULL)
		return EINVAL;

	len = strlen(params) + 1;
	argc = 0;

	/*
	 * Parse a string, containing tokens delimited by white space,
	 * into an argument vector
	 */
	for (ap = args; ap < &args[4] &&
	    (*ap = strsep(&params, " \t")) != NULL;) {
		if (**ap != '\0') {
			argc++;
			ap++;
		}
	}

	kprintf("\nCrypto target init function called, argc = %d!!\n", argc);
	if (argc < 4) {
		kprintf("not enough arguments for target crypt\n");
		return ENOMEM; /* XXX */
	}

	kprintf("Crypto target - device name %s -- algorithm %s\n\n", args[0], args[1]);

	if ((priv = kmalloc(sizeof(dm_target_crypt_config_t), M_DMCRYPT, M_NOWAIT))
	    == NULL) {
		kprintf("kmalloc in dm_target_crypt_init failed, M_NOWAIT to blame\n");
		return ENOMEM;
	}

	/* Insert dmp to global pdev list */
	if ((priv->pdev = dm_pdev_insert(args[0])) == NULL) {
		kprintf("dm_pdev_insert failed\n");
		return ENOENT;
	}

	if (!strncmp(args[1], "aes-", 4)) {
		priv->crypto_alg = CRYPTO_AES_CBC;
		klen = atoi(args[1]+4);
		if (klen != 128 && klen != 192 && klen != 256)
			goto notsup;
		priv->crypto_klen = klen;

	} else if (!strncmp(args[1], "blowfish-", 9)) {
		priv->crypto_alg = CRYPTO_BLF_CBC;
		klen = atoi(args[1]+9);
		if (klen < 128 || klen > 448 || (klen % 8) != 0)
			goto notsup;
		priv->crypto_klen = klen;

	} else if (!strcmp(args[1], "3DES")) {
		priv->crypto_alg = CRYPTO_3DES_CBC;
		priv->crypto_klen = 168;

	} else if (!strncmp(args[1], "camellia-", 9)) {
		priv->crypto_alg = CRYPTO_CAMELLIA_CBC;
		klen = atoi(args[1]+9);
		if (klen != 128 && klen != 192 && klen != 256)
			goto notsup;
		priv->crypto_klen = klen;

	} else if (!strcmp(args[1], "skipjack")) {
		priv->crypto_alg = CRYPTO_SKIPJACK_CBC;
		priv->crypto_klen = 80;

	} else if (!strcmp(args[1], "CAST-128")) {
		priv->crypto_alg = CRYPTO_CAST_CBC;
		priv->crypto_klen = 128;

	} else if (!strcmp(args[1], "null")) {
		priv->crypto_alg = CRYPTO_NULL_CBC;
		priv->crypto_klen = 128;
	}

	/* Save length of param string */
	priv->params_len = len;

	*target_config = priv;

	dmv->dev_type = DM_CRYPTO_DEV;

	priv->crypto_session.cri_alg = priv->crypto_alg;
	priv->crypto_session.cri_klen = priv->crypto_klen;
	priv->crypto_session.cri_mlen = 0;
	error = pkcs5_pbkdf2(args[3], strlen(args[3]),
			     "This is the salt", 16, /* XXX !!!!!!!!!!!!! */
			     (u_int8_t *)priv->crypto_key, priv->crypto_klen >> 3,
			     1000);
	if (error)
		panic("dm_target_crypt: pkcs5_pbkdf2 returned error!");

	kprintf("priv->crypto_klen >> 3 = %d\n", priv->crypto_klen >> 3);


	if (!strcmp(args[2], "essiv")) {
		essiv_hash_mkey(priv);
		priv->crypto_ivgen = essiv_ivgen;
	} else {
		SHA512_Init(&priv->essivsha512_ctx);
		SHA512_Update(&priv->essivsha512_ctx, (u_int8_t*)priv->crypto_key, priv->crypto_klen >> 3);
		priv->crypto_ivgen = alt_ivgen;
	}

	priv->crypto_session.cri_key = (u_int8_t *)priv->crypto_key;
	priv->crypto_session.cri_next = NULL;

	error = crypto_newsession(&priv->crypto_sid,
				  &priv->crypto_session,
				  CRYPTOCAP_F_SOFTWARE | CRYPTOCAP_F_HARDWARE);
	if (error) {
		kprintf("Error during crypto_newsession, error = %d\n", error);
		return error;
	}

	/* XXX: eventually support some on-disk metadata */
	return 0;

notsup:
	kprintf("returning ENOTSUP from crypt_init thingie... notsup label\n");
	return ENOTSUP;
}

/* Status routine called to get params string. */
char *
dm_target_crypt_status(void *target_config)
{
	dm_target_crypt_config_t *priv;
	char *params;

	priv = target_config;

	if ((params = kmalloc(DM_MAX_PARAMS_SIZE, M_DMCRYPT, M_WAITOK)) == NULL)
		return NULL;

	ksnprintf(params, DM_MAX_PARAMS_SIZE, "%s",
	    priv->pdev->name);

	return params;
}

/* Strategy routine called from dm_strategy. */
/*
 * Do IO operation, called from dmstrategy routine.
 */
int
dm_target_crypt_strategy(dm_table_entry_t * table_en, struct buf * bp)
{
	struct bio *bio;

	dm_target_crypt_config_t *priv;
	priv = table_en->target_config;

	/* Get rid of stuff we can't really handle */
	if ((bp->b_cmd == BUF_CMD_READ) || (bp->b_cmd == BUF_CMD_WRITE)) {
		if (((bp->b_bcount % DEV_BSIZE) != 0) || (bp->b_bcount == 0)) {
			kprintf("dm_target_crypt_strategy: can't really handle bp->b_bcount = %d\n", bp->b_bcount);
			bp->b_error = EINVAL;
			bp->b_flags |= B_ERROR | B_INVAL;
			biodone(&bp->b_bio1);
			return 0;
		}
	}

	switch (bp->b_cmd) {
	case BUF_CMD_READ:
		bio = push_bio(&bp->b_bio1);
		bio->bio_offset = bp->b_bio1.bio_offset;
		bio->bio_caller_info1.ptr = priv;
		bio->bio_done = dm_target_crypt_read_done;
		vn_strategy(priv->pdev->pdev_vnode, bio);
		break;

	case BUF_CMD_WRITE:
		bio = push_bio(&bp->b_bio1);
		bio->bio_offset = bp->b_bio1.bio_offset;
		bio->bio_caller_info1.ptr = priv;
		bio->bio_done = dm_target_crypt_write_done;
		dm_target_crypt_work(priv, bio);
		break;

	default:
		vn_strategy(priv->pdev->pdev_vnode, &bp->b_bio1);		
	}

	return 0;

}

int
dm_target_crypt_destroy(dm_table_entry_t * table_en)
{
	dm_target_crypt_config_t *priv;

	priv = table_en->target_config;

	if (priv == NULL)
		return 0;

	dm_pdev_decr(priv->pdev);

	/* Unbusy target so we can unload it */
	dm_target_unbusy(table_en->target);

	kfree(priv, M_DMCRYPT);

	table_en->target_config = NULL;

	return 0;
}

int
dm_target_crypt_deps(dm_table_entry_t * table_en, prop_array_t prop_array)
{
	dm_target_crypt_config_t *priv;
	struct vattr va;

	int error;

	if (table_en->target_config == NULL)
		return ENOENT;

	priv = table_en->target_config;

	if ((error = VOP_GETATTR(priv->pdev->pdev_vnode, &va)) != 0)
		return error;

	prop_array_add_uint64(prop_array, (uint64_t) makeudev(va.va_rmajor, va.va_rminor));

	return 0;
}

/* Unsupported for this target. */
int
dm_target_crypt_upcall(dm_table_entry_t * table_en, struct buf * bp)
{
	return 0;
}
