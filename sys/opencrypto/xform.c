/*	$FreeBSD: src/sys/opencrypto/xform.c,v 1.10 2008/10/23 15:53:51 des Exp $	*/
/*	$OpenBSD: xform.c,v 1.16 2001/08/28 12:20:43 ben Exp $	*/
/*-
 * The authors of this code are John Ioannidis (ji@tla.org),
 * Angelos D. Keromytis (kermit@csd.uch.gr) and
 * Niels Provos (provos@physnet.uni-hamburg.de).
 *
 * This code was written by John Ioannidis for BSD/OS in Athens, Greece,
 * in November 1995.
 *
 * Ported to OpenBSD and NetBSD, with additional transforms, in December 1996,
 * by Angelos D. Keromytis.
 *
 * Additional transforms and features in 1997 and 1998 by Angelos D. Keromytis
 * and Niels Provos.
 *
 * Additional features in 1999 by Angelos D. Keromytis.
 *
 * Copyright (C) 1995, 1996, 1997, 1998, 1999 by John Ioannidis,
 * Angelos D. Keromytis and Niels Provos.
 *
 * Copyright (C) 2001, Angelos D. Keromytis.
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

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/malloc.h>
#include <sys/sysctl.h>
#include <sys/errno.h>
#include <sys/time.h>
#include <sys/kernel.h>
#include <machine/cpu.h>

#include <crypto/blowfish/blowfish.h>
#include <crypto/camellia/camellia.h>
#include <crypto/des/des.h>
#include <crypto/rijndael/rijndael.h>
#include <crypto/serpent/serpent.h>
#include <crypto/sha1.h>
#include <crypto/twofish/twofish.h>
#include <crypto/rmd160/rmd160.h>

#include <opencrypto/cast.h>
#include <opencrypto/deflate.h>
#include <opencrypto/gmac.h>
#include <opencrypto/skipjack.h>

#include <sys/md5.h>

#include <opencrypto/cryptodev.h>
#include <opencrypto/xform.h>

static	void null_encrypt(caddr_t, u_int8_t *, u_int8_t *);
static	void null_decrypt(caddr_t, u_int8_t *, u_int8_t *);
static	int null_setkey(void *, u_int8_t *, int);

static	int des1_setkey(void *, u_int8_t *, int);
static	int des3_setkey(void *, u_int8_t *, int);
static	int blf_setkey(void *, u_int8_t *, int);
static	int cast5_setkey(void *, u_int8_t *, int);
static	int skipjack_setkey(void *, u_int8_t *, int);
static	int rijndael128_setkey(void *, u_int8_t *, int);
static	int aes_xts_setkey(void *, u_int8_t *, int);
static	int aes_ctr_setkey(void *, u_int8_t *, int);
static	int cml_setkey(void *, u_int8_t *, int);
static	int twofish128_setkey(void *, u_int8_t *, int);
static	int serpent128_setkey(void *, u_int8_t *, int);
static	int twofish_xts_setkey(void *, u_int8_t *, int);
static	int serpent_xts_setkey(void *, u_int8_t *, int);

static	void des1_encrypt(caddr_t, u_int8_t *, u_int8_t *);
static	void des3_encrypt(caddr_t, u_int8_t *, u_int8_t *);
static	void blf_encrypt(caddr_t, u_int8_t *, u_int8_t *);
static	void cast5_encrypt(caddr_t, u_int8_t *, u_int8_t *);
static	void skipjack_encrypt(caddr_t, u_int8_t *, u_int8_t *);
static	void rijndael128_encrypt(caddr_t, u_int8_t *, u_int8_t *);
static	void aes_xts_encrypt(caddr_t, u_int8_t *, u_int8_t *);
static	void cml_encrypt(caddr_t, u_int8_t *, u_int8_t *);
static	void twofish128_encrypt(caddr_t, u_int8_t *, u_int8_t *);
static	void serpent128_encrypt(caddr_t, u_int8_t *, u_int8_t *);
static	void twofish_xts_encrypt(caddr_t, u_int8_t *, u_int8_t *);
static	void serpent_xts_encrypt(caddr_t, u_int8_t *, u_int8_t *);

static	void des1_decrypt(caddr_t, u_int8_t *, u_int8_t *);
static	void des3_decrypt(caddr_t, u_int8_t *, u_int8_t *);
static	void blf_decrypt(caddr_t, u_int8_t *, u_int8_t *);
static	void cast5_decrypt(caddr_t, u_int8_t *, u_int8_t *);
static	void skipjack_decrypt(caddr_t, u_int8_t *, u_int8_t *);
static	void rijndael128_decrypt(caddr_t, u_int8_t *, u_int8_t *);
static	void aes_xts_decrypt(caddr_t, u_int8_t *, u_int8_t *);
static	void cml_decrypt(caddr_t, u_int8_t *, u_int8_t *);
static	void twofish128_decrypt(caddr_t, u_int8_t *, u_int8_t *);
static	void serpent128_decrypt(caddr_t, u_int8_t *, u_int8_t *);
static	void twofish_xts_decrypt(caddr_t, u_int8_t *, u_int8_t *);
static	void serpent_xts_decrypt(caddr_t, u_int8_t *, u_int8_t *);

static	void aes_ctr_crypt(caddr_t, u_int8_t *, u_int8_t *);

static	void aes_ctr_reinit(caddr_t, u_int8_t *);
static	void aes_xts_reinit(caddr_t, u_int8_t *);
static	void aes_gcm_reinit(caddr_t, u_int8_t *);
static	void twofish_xts_reinit(caddr_t, u_int8_t *);
static	void serpent_xts_reinit(caddr_t, u_int8_t *);

static	void null_init(void *);
static	int null_update(void *, u_int8_t *, u_int16_t);
static	void null_final(u_int8_t *, void *);
static	int MD5Update_int(void *, u_int8_t *, u_int16_t);
static	void SHA1Init_int(void *);
static	int SHA1Update_int(void *, u_int8_t *, u_int16_t);
static	void SHA1Final_int(u_int8_t *, void *);
static	int RMD160Update_int(void *, u_int8_t *, u_int16_t);
static	int SHA256Update_int(void *, u_int8_t *, u_int16_t);
static	int SHA384Update_int(void *, u_int8_t *, u_int16_t);
static	int SHA512Update_int(void *, u_int8_t *, u_int16_t);

static	u_int32_t deflate_compress(u_int8_t *, u_int32_t, u_int8_t **);
static	u_int32_t deflate_decompress(u_int8_t *, u_int32_t, u_int8_t **);

#define AES_XTS_ALPHA		0x87	/* GF(2^128) generator polynomial */
#define AESCTR_NONCESIZE	4

struct aes_xts_ctx {
	rijndael_ctx key1;
	rijndael_ctx key2;
};

struct aes_ctr_ctx {
	u_int32_t	ac_ek[4*(14 + 1)];
	u_int8_t	ac_block[AESCTR_BLOCK_LEN];
	int		ac_nr;
};

struct twofish_xts_ctx {
	twofish_ctx key1;
	twofish_ctx key2;
};

struct serpent_xts_ctx {
	serpent_ctx key1;
	serpent_ctx key2;
};

/* Helper */
static void aes_xts_crypt(struct aes_xts_ctx *, u_int8_t *, u_int8_t *, u_int);
static void twofish_xts_crypt(struct twofish_xts_ctx *, u_int8_t *, u_int8_t *,
    u_int);
static void serpent_xts_crypt(struct serpent_xts_ctx *, u_int8_t *, u_int8_t *,
    u_int);

MALLOC_DEFINE(M_XDATA, "xform", "xform data buffers");

/* Encryption instances */
struct enc_xform enc_xform_null = {
	CRYPTO_NULL_CBC, "NULL",
	/* NB: blocksize of 4 is to generate a properly aligned ESP header */
	NULL_BLOCK_LEN, NULL_BLOCK_LEN, 0, 256, /* 2048 bits, max key */
	sizeof(int),	/* NB: context isn't used */
	null_encrypt,
	null_decrypt,
	null_setkey,
	NULL,
};

struct enc_xform enc_xform_des = {
	CRYPTO_DES_CBC, "DES",
	DES_BLOCK_LEN, DES_BLOCK_LEN, 8, 8,
	sizeof(des_key_schedule),
	des1_encrypt,
	des1_decrypt,
	des1_setkey,
	NULL,
};

struct enc_xform enc_xform_3des = {
	CRYPTO_3DES_CBC, "3DES",
	DES3_BLOCK_LEN, DES3_BLOCK_LEN, 24, 24,
	3 * sizeof(des_key_schedule),
	des3_encrypt,
	des3_decrypt,
	des3_setkey,
	NULL,
};

struct enc_xform enc_xform_blf = {
	CRYPTO_BLF_CBC, "Blowfish",
	BLOWFISH_BLOCK_LEN, BLOWFISH_BLOCK_LEN, 5, 56 /* 448 bits, max key */,
	sizeof(BF_KEY),
	blf_encrypt,
	blf_decrypt,
	blf_setkey,
	NULL,
};

struct enc_xform enc_xform_cast5 = {
	CRYPTO_CAST_CBC, "CAST-128",
	CAST128_BLOCK_LEN, CAST128_BLOCK_LEN, 5, 16,
	sizeof(cast_key),
	cast5_encrypt,
	cast5_decrypt,
	cast5_setkey,
	NULL,
};

struct enc_xform enc_xform_skipjack = {
	CRYPTO_SKIPJACK_CBC, "Skipjack",
	SKIPJACK_BLOCK_LEN, SKIPJACK_BLOCK_LEN, 10, 10,
	10 * (sizeof(u_int8_t *) + 0x100), /* NB: all needed memory */
	skipjack_encrypt,
	skipjack_decrypt,
	skipjack_setkey,
	NULL,
};

struct enc_xform enc_xform_rijndael128 = {
	CRYPTO_RIJNDAEL128_CBC, "Rijndael-128/AES",
	RIJNDAEL128_BLOCK_LEN, RIJNDAEL128_BLOCK_LEN, 8, 32,
	sizeof(rijndael_ctx),
	rijndael128_encrypt,
	rijndael128_decrypt,
	rijndael128_setkey,
	NULL,
};

struct enc_xform enc_xform_aes_xts = {
	CRYPTO_AES_XTS, "AES-XTS",
	AES_XTS_BLOCK_LEN, AES_XTS_IV_LEN, 32, 64,
	sizeof(struct aes_xts_ctx),
	aes_xts_encrypt,
	aes_xts_decrypt,
	aes_xts_setkey,
	aes_xts_reinit,
};

struct enc_xform enc_xform_aes_ctr = {
	CRYPTO_AES_CTR, "AES-CTR",
	AESCTR_BLOCK_LEN, AESCTR_IV_LEN, 16+4, 32+4,
	sizeof(struct aes_ctr_ctx),
	aes_ctr_crypt,
	aes_ctr_crypt,
	aes_ctr_setkey,
	aes_ctr_reinit,
};

struct enc_xform enc_xform_aes_gcm = {
	CRYPTO_AES_GCM_16, "AES-GCM",
	AESGCM_BLOCK_LEN, AESGCM_IV_LEN, 16+4, 32+4,
	sizeof(struct aes_ctr_ctx),
	aes_ctr_crypt,
	aes_ctr_crypt,
	aes_ctr_setkey,
	aes_gcm_reinit,
};

struct enc_xform enc_xform_aes_gmac = {
	CRYPTO_AES_GMAC, "AES-GMAC",
	AESGMAC_BLOCK_LEN, AESGMAC_IV_LEN, 16+4, 32+4,
	0, /* NB: no context */
	NULL,
	NULL,
	NULL,
	NULL,
};

struct enc_xform enc_xform_arc4 = {
	CRYPTO_ARC4, "ARC4",
	1, 1, 1, 32,
	0, /* NB: no context */
	NULL,
	NULL,
	NULL,
	NULL,
};

struct enc_xform enc_xform_camellia = {
	CRYPTO_CAMELLIA_CBC, "Camellia",
	CAMELLIA_BLOCK_LEN, CAMELLIA_BLOCK_LEN, 8, 32,
	sizeof(camellia_ctx),
	cml_encrypt,
	cml_decrypt,
	cml_setkey,
	NULL,
};

struct enc_xform enc_xform_twofish = {
	CRYPTO_TWOFISH_CBC, "Twofish",
	TWOFISH_BLOCK_LEN, TWOFISH_BLOCK_LEN, 8, 32,
	sizeof(twofish_ctx),
	twofish128_encrypt,
	twofish128_decrypt,
	twofish128_setkey,
	NULL,
};

struct enc_xform enc_xform_serpent = {
	CRYPTO_SERPENT_CBC, "Serpent",
	SERPENT_BLOCK_LEN, SERPENT_BLOCK_LEN, 8, 32,
	sizeof(serpent_ctx),
	serpent128_encrypt,
	serpent128_decrypt,
	serpent128_setkey,
	NULL,
};

struct enc_xform enc_xform_twofish_xts = {
	CRYPTO_TWOFISH_XTS, "TWOFISH-XTS",
	TWOFISH_XTS_BLOCK_LEN, TWOFISH_XTS_IV_LEN, 32, 64,
	sizeof(struct twofish_xts_ctx),
	twofish_xts_encrypt,
	twofish_xts_decrypt,
	twofish_xts_setkey,
	twofish_xts_reinit,
};

struct enc_xform enc_xform_serpent_xts = {
	CRYPTO_SERPENT_XTS, "SERPENT-XTS",
	SERPENT_XTS_BLOCK_LEN, SERPENT_XTS_IV_LEN, 32, 64,
	sizeof(struct serpent_xts_ctx),
	serpent_xts_encrypt,
	serpent_xts_decrypt,
	serpent_xts_setkey,
	serpent_xts_reinit,
};


/* Authentication instances */
struct auth_hash auth_hash_null = {
	CRYPTO_NULL_HMAC, "NULL-HMAC",
	0, NULL_HASH_LEN, NULL_HMAC_BLOCK_LEN,
	sizeof(int),	/* NB: context isn't used */
	null_init, NULL, NULL, null_update, null_final
};

struct auth_hash auth_hash_hmac_md5 = {
	CRYPTO_MD5_HMAC, "HMAC-MD5",
	16, MD5_HASH_LEN, MD5_HMAC_BLOCK_LEN, sizeof(MD5_CTX),
	(void (*) (void *)) MD5Init, NULL, NULL,
	MD5Update_int,
	(void (*) (u_int8_t *, void *)) MD5Final
};

struct auth_hash auth_hash_hmac_sha1 = {
	CRYPTO_SHA1_HMAC, "HMAC-SHA1",
	20, SHA1_HASH_LEN, SHA1_HMAC_BLOCK_LEN, sizeof(SHA1_CTX),
	SHA1Init_int, NULL, NULL,
	SHA1Update_int, SHA1Final_int
};

struct auth_hash auth_hash_hmac_ripemd_160 = {
	CRYPTO_RIPEMD160_HMAC, "HMAC-RIPEMD-160",
	20, RIPEMD160_HASH_LEN, RIPEMD160_HMAC_BLOCK_LEN, sizeof(RMD160_CTX),
	(void (*)(void *)) RMD160Init, NULL, NULL,
	RMD160Update_int,
	(void (*)(u_int8_t *, void *)) RMD160Final
};

struct auth_hash auth_hash_key_md5 = {
	CRYPTO_MD5_KPDK, "Keyed MD5",
	0, MD5_KPDK_HASH_LEN, 0, sizeof(MD5_CTX),
	(void (*)(void *)) MD5Init, NULL, NULL,
	MD5Update_int,
	(void (*)(u_int8_t *, void *)) MD5Final
};

struct auth_hash auth_hash_key_sha1 = {
	CRYPTO_SHA1_KPDK, "Keyed SHA1",
	0, SHA1_KPDK_HASH_LEN, 0, sizeof(SHA1_CTX),
	SHA1Init_int, NULL, NULL,
	SHA1Update_int, SHA1Final_int
};

struct auth_hash auth_hash_hmac_sha2_256 = {
	CRYPTO_SHA2_256_HMAC, "HMAC-SHA2-256",
	32, SHA2_256_HASH_LEN, SHA2_256_HMAC_BLOCK_LEN, sizeof(SHA256_CTX),
	(void (*)(void *)) SHA256_Init, NULL, NULL,
	SHA256Update_int,
	(void (*)(u_int8_t *, void *)) SHA256_Final
};

struct auth_hash auth_hash_hmac_sha2_384 = {
	CRYPTO_SHA2_384_HMAC, "HMAC-SHA2-384",
	48, SHA2_384_HASH_LEN, SHA2_384_HMAC_BLOCK_LEN, sizeof(SHA384_CTX),
	(void (*)(void *)) SHA384_Init, NULL, NULL,
	SHA384Update_int,
	(void (*)(u_int8_t *, void *)) SHA384_Final
};

struct auth_hash auth_hash_hmac_sha2_512 = {
	CRYPTO_SHA2_512_HMAC, "HMAC-SHA2-512",
	64, SHA2_512_HASH_LEN, SHA2_512_HMAC_BLOCK_LEN, sizeof(SHA512_CTX),
	(void (*)(void *)) SHA512_Init, NULL, NULL,
	SHA512Update_int,
	(void (*)(u_int8_t *, void *)) SHA512_Final
};

struct auth_hash auth_hash_gmac_aes_128 = {
	CRYPTO_AES_128_GMAC, "GMAC-AES-128",
	16+4, 16, 16, sizeof(AES_GMAC_CTX),
	(void (*)(void *)) AES_GMAC_Init,
	(int  (*)(void *, const u_int8_t *, u_int16_t)) AES_GMAC_Setkey,
	(void (*)(void *, const u_int8_t *, u_int16_t)) AES_GMAC_Reinit,
	(int  (*)(void *, u_int8_t *, u_int16_t)) AES_GMAC_Update,
	(void (*)(u_int8_t *, void *)) AES_GMAC_Final
};

struct auth_hash auth_hash_gmac_aes_192 = {
	CRYPTO_AES_192_GMAC, "GMAC-AES-192",
	24+4, 16, 16, sizeof(AES_GMAC_CTX),
	(void (*)(void *)) AES_GMAC_Init,
	(int  (*)(void *, const u_int8_t *, u_int16_t)) AES_GMAC_Setkey,
	(void (*)(void *, const u_int8_t *, u_int16_t)) AES_GMAC_Reinit,
	(int  (*)(void *, u_int8_t *, u_int16_t)) AES_GMAC_Update,
	(void (*)(u_int8_t *, void *)) AES_GMAC_Final
};

struct auth_hash auth_hash_gmac_aes_256 = {
	CRYPTO_AES_256_GMAC, "GMAC-AES-256",
	32+4, 16, 16, sizeof(AES_GMAC_CTX),
	(void (*)(void *)) AES_GMAC_Init,
	(int  (*)(void *, const u_int8_t *, u_int16_t)) AES_GMAC_Setkey,
	(void (*)(void *, const u_int8_t *, u_int16_t)) AES_GMAC_Reinit,
	(int  (*)(void *, u_int8_t *, u_int16_t)) AES_GMAC_Update,
	(void (*)(u_int8_t *, void *)) AES_GMAC_Final
};

/* Compression instance */
struct comp_algo comp_algo_deflate = {
	CRYPTO_DEFLATE_COMP, "Deflate",
	90, deflate_compress,
	deflate_decompress
};

/*
 * Encryption wrapper routines.
 */

static void
null_encrypt(caddr_t key, u_int8_t *blk, u_int8_t *iv)
{
}

static void
null_decrypt(caddr_t key, u_int8_t *blk, u_int8_t *iv)
{
}

static int
null_setkey(void *sched, u_int8_t *key, int len)
{
	return 0;
}

static void
des1_encrypt(caddr_t key, u_int8_t *blk, u_int8_t *iv)
{
	des_cblock *cb = (des_cblock *) blk;
	des_key_schedule *p = (des_key_schedule *) key;

	des_ecb_encrypt(cb, cb, p[0], DES_ENCRYPT);
}

static void
des1_decrypt(caddr_t key, u_int8_t *blk, u_int8_t *iv)
{
	des_cblock *cb = (des_cblock *) blk;
	des_key_schedule *p = (des_key_schedule *) key;

	des_ecb_encrypt(cb, cb, p[0], DES_DECRYPT);
}

static int
des1_setkey(void *sched, u_int8_t *key, int len)
{
	return des_set_key((des_cblock *)key, sched);
}

static void
des3_encrypt(caddr_t key, u_int8_t *blk, u_int8_t *iv)
{
	des_cblock *cb = (des_cblock *) blk;
	des_key_schedule *p = (des_key_schedule *) key;

	des_ecb3_encrypt(cb, cb, p[0], p[1], p[2], DES_ENCRYPT);
}

static void
des3_decrypt(caddr_t key, u_int8_t *blk, u_int8_t *iv)
{
	des_cblock *cb = (des_cblock *) blk;
	des_key_schedule *p = (des_key_schedule *) key;

	des_ecb3_encrypt(cb, cb, p[0], p[1], p[2], DES_DECRYPT);
}

static int
des3_setkey(void *sched, u_int8_t *key, int len)
{
	des_key_schedule *p;

	p = sched;
	if (des_set_key((des_cblock *)(key +  0), p[0]) < 0 ||
	    des_set_key((des_cblock *)(key +  8), p[1]) < 0 ||
	    des_set_key((des_cblock *)(key + 16), p[2]) < 0)
		return -1;

	return 0;
}

static void
blf_encrypt(caddr_t key, u_int8_t *blk, u_int8_t *iv)
{
	BF_LONG t[2];

	memcpy(t, blk, sizeof (t));
	t[0] = ntohl(t[0]);
	t[1] = ntohl(t[1]);
	/* NB: BF_encrypt expects the block in host order! */
	BF_encrypt(t, (BF_KEY *) key);
	t[0] = htonl(t[0]);
	t[1] = htonl(t[1]);
	memcpy(blk, t, sizeof (t));
}

static void
blf_decrypt(caddr_t key, u_int8_t *blk, u_int8_t *iv)
{
	BF_LONG t[2];

	memcpy(t, blk, sizeof (t));
	t[0] = ntohl(t[0]);
	t[1] = ntohl(t[1]);
	/* NB: BF_decrypt expects the block in host order! */
	BF_decrypt(t, (BF_KEY *) key);
	t[0] = htonl(t[0]);
	t[1] = htonl(t[1]);
	memcpy(blk, t, sizeof (t));
}

static int
blf_setkey(void *sched, u_int8_t *key, int len)
{
	BF_set_key(sched, len, key);
	return 0;
}

static void
cast5_encrypt(caddr_t key, u_int8_t *blk, u_int8_t *iv)
{
	cast_encrypt((cast_key *) key, blk, blk);
}

static void
cast5_decrypt(caddr_t key, u_int8_t *blk, u_int8_t *iv)
{
	cast_decrypt((cast_key *) key, blk, blk);
}

static int
cast5_setkey(void *sched, u_int8_t *key, int len)
{
	cast_setkey(sched, key, len);
	return 0;
}

static void
skipjack_encrypt(caddr_t key, u_int8_t *blk, u_int8_t *iv)
{
	skipjack_forwards(blk, blk, (u_int8_t **) key);
}

static void
skipjack_decrypt(caddr_t key, u_int8_t *blk, u_int8_t *iv)
{
	skipjack_backwards(blk, blk, (u_int8_t **) key);
}

static int
skipjack_setkey(void *sched, u_int8_t *key, int len)
{
	u_int8_t **key_tables = sched;
	u_int8_t *table = (u_int8_t *)&key_tables[10];
	int k;

	for (k = 0; k < 10; k++) {
		key_tables[k] = table;
		table += 0x100;
	}
	subkey_table_gen(key, sched);

	return 0;
}

static void
rijndael128_encrypt(caddr_t key, u_int8_t *blk, u_int8_t *iv)
{
	rijndael_encrypt((rijndael_ctx *) key, (u_char *) blk, (u_char *) blk);
}

static void
rijndael128_decrypt(caddr_t key, u_int8_t *blk, u_int8_t *iv)
{
	rijndael_decrypt(((rijndael_ctx *) key), (u_char *) blk,
			 (u_char *) blk);
}

static int
rijndael128_setkey(void *sched, u_int8_t *key, int len)
{
	if (len != 16 && len != 24 && len != 32)
		return (EINVAL);

	rijndael_set_key(sched, (u_char *) key, len * 8);

	return 0;
}

void
aes_xts_reinit(caddr_t key, u_int8_t *iv)
{
	struct aes_xts_ctx *ctx = (struct aes_xts_ctx *)key;
#if 0
	u_int64_t blocknum;
	u_int i;
#endif

#if 0
	/*
	 * Prepare tweak as E_k2(IV). IV is specified as LE representation
	 * of a 64-bit block number which we allow to be passed in directly.
	 */
	/* XXX: possibly use htole64? */
#endif
	/* Last 64 bits of IV are always zero */
	bzero(iv + AES_XTS_IV_LEN, AES_XTS_IV_LEN);

	rijndael_encrypt(&ctx->key2, iv, iv);
}

void
aes_xts_crypt(struct aes_xts_ctx *ctx, u_int8_t *data, u_int8_t *iv,
	      u_int do_encrypt)
{
	u_int8_t block[AES_XTS_BLOCK_LEN];
	u_int i, carry_in, carry_out;

	for (i = 0; i < AES_XTS_BLOCK_LEN; i++)
		block[i] = data[i] ^ iv[i];

	if (do_encrypt)
		rijndael_encrypt(&ctx->key1, block, data);
	else
		rijndael_decrypt(&ctx->key1, block, data);

	for (i = 0; i < AES_XTS_BLOCK_LEN; i++)
		data[i] ^= iv[i];

	/* Exponentiate tweak */
	carry_in = 0;
	for (i = 0; i < AES_XTS_BLOCK_LEN; i++) {
		carry_out = iv[i] & 0x80;
		iv[i] = (iv[i] << 1) | (carry_in ? 1 : 0);
		carry_in = carry_out;
	}
	if (carry_in)
		iv[0] ^= AES_XTS_ALPHA;
	bzero(block, sizeof(block));
}

void
aes_xts_encrypt(caddr_t key, u_int8_t *data, u_int8_t *iv)
{
	aes_xts_crypt((struct aes_xts_ctx *)key, data, iv, 1);
}

void
aes_xts_decrypt(caddr_t key, u_int8_t *data, u_int8_t *iv)
{
	aes_xts_crypt((struct aes_xts_ctx *)key, data, iv, 0);
}

int
aes_xts_setkey(void *sched, u_int8_t *key, int len)
{
	struct aes_xts_ctx *ctx;

	if (len != 32 && len != 64)
		return -1;

	ctx = sched;
	rijndael_set_key(&ctx->key1, key, len * 4);
	rijndael_set_key(&ctx->key2, key + (len / 2), len * 4);

	return 0;
}

void
aes_ctr_reinit(caddr_t key, u_int8_t *iv)
{
	struct aes_ctr_ctx *ctx;

	ctx = (struct aes_ctr_ctx *)key;
	bcopy(iv, iv + AESCTR_NONCESIZE, AESCTR_IV_LEN);
	bcopy(ctx->ac_block, iv, AESCTR_NONCESIZE);

	/* reset counter */
	bzero(iv + AESCTR_NONCESIZE + AESCTR_IV_LEN, 4);
}

void
aes_ctr_crypt(caddr_t key, u_int8_t *data, u_int8_t *iv)
{
	struct aes_ctr_ctx *ctx;
	u_int8_t keystream[AESCTR_BLOCK_LEN];
	int i;

	ctx = (struct aes_ctr_ctx *)key;
	/* increment counter */
	for (i = AESCTR_BLOCK_LEN - 1;
	i >= AESCTR_NONCESIZE + AESCTR_IV_LEN; i--)
		if (++iv[i])   /* continue on overflow */
			break;
	rijndaelEncrypt(ctx->ac_ek, ctx->ac_nr, iv, keystream);
	for (i = 0; i < AESCTR_BLOCK_LEN; i++)
		data[i] ^= keystream[i];
	bzero(keystream, sizeof(keystream));
}

int
aes_ctr_setkey(void *sched, u_int8_t *key, int len)
{
	struct aes_ctr_ctx *ctx;

	len -= AESCTR_NONCESIZE;
	if (len < 0)
		return -1;
	if (!(len == 16 || len == 24 || len == 32))
		return -1; /* invalid key bits */

	ctx = sched;
	ctx->ac_nr = rijndaelKeySetupEnc(ctx->ac_ek, key, len * 8);
	if (ctx->ac_nr == 0) {
		bzero(ctx, sizeof(struct aes_ctr_ctx));
		return -1;
	}

	bcopy(key + len, ctx->ac_block, AESCTR_NONCESIZE);

	return 0;
}

static void
aes_gcm_reinit(caddr_t key, u_int8_t *iv)
{
	struct aes_ctr_ctx *ctx;

	ctx = (struct aes_ctr_ctx *)key;
	bcopy(iv, ctx->ac_block + AESCTR_NONCESIZE, AESCTR_IV_LEN);

	/* reset counter */
	bzero(ctx->ac_block + AESCTR_NONCESIZE + AESCTR_IV_LEN, 4);
	ctx->ac_block[AESCTR_BLOCK_LEN - 1] = 1; /* GCM starts with 1 */
}

static void
cml_encrypt(caddr_t key, u_int8_t *blk, u_int8_t *iv)
{
	camellia_encrypt((camellia_ctx *) key, (u_char *) blk, (u_char *) blk);
}

static void
cml_decrypt(caddr_t key, u_int8_t *blk, u_int8_t *iv)
{
	camellia_decrypt(((camellia_ctx *) key), (u_char *) blk,
			 (u_char *) blk);
}

static int
cml_setkey(void *sched, u_int8_t *key, int len)
{
	if (len != 16 && len != 24 && len != 32)
		return (EINVAL);

	camellia_set_key(sched, key, len * 8);

	return 0;
}

static void
twofish128_encrypt(caddr_t key, u_int8_t *blk, u_int8_t *iv)
{
	twofish_encrypt((const twofish_ctx *) key, blk, blk);
}

static void
twofish128_decrypt(caddr_t key, u_int8_t *blk, u_int8_t *iv)
{
	twofish_decrypt(((const twofish_ctx *) key), blk, blk);
}

static int
twofish128_setkey(void *sched, u_int8_t *key, int len)
{
	if (len != 16 && len != 24 && len != 32)
		return (EINVAL);

	twofish_set_key(sched, key, len * 8);

	return 0;
}

static void
serpent128_encrypt(caddr_t key, u_int8_t *blk, u_int8_t *iv)
{
	serpent_encrypt((const serpent_ctx *) key, blk, blk);
}

static void
serpent128_decrypt(caddr_t key, u_int8_t *blk, u_int8_t *iv)
{
	serpent_decrypt(((const serpent_ctx *) key), blk, blk);
}

static int
serpent128_setkey(void *sched, u_int8_t *key, int len)
{
	if (len != 16 && len != 24 && len != 32)
		return (EINVAL);

	serpent_set_key(sched, key, len * 8);

	return 0;
}


void
twofish_xts_reinit(caddr_t key, u_int8_t *iv)
{
	struct twofish_xts_ctx *ctx = (struct twofish_xts_ctx *)key;
#if 0
	u_int64_t blocknum;
#endif

#if 0
	/*
	 * Prepare tweak as E_k2(IV). IV is specified as LE representation
	 * of a 64-bit block number which we allow to be passed in directly.
	 */
	/* XXX: possibly use htole64? */
#endif
	/* Last 64 bits of IV are always zero */
	bzero(iv + TWOFISH_XTS_IV_LEN, TWOFISH_XTS_IV_LEN);

	twofish_encrypt(&ctx->key2, iv, iv);
}

void
twofish_xts_crypt(struct twofish_xts_ctx *ctx, u_int8_t *data, u_int8_t *iv,
    u_int do_encrypt)
{
	u_int8_t block[TWOFISH_XTS_BLOCK_LEN];
	u_int i, carry_in, carry_out;

	for (i = 0; i < TWOFISH_XTS_BLOCK_LEN; i++)
		block[i] = data[i] ^ iv[i];

	if (do_encrypt)
		twofish_encrypt(&ctx->key1, block, data);
	else
		twofish_decrypt(&ctx->key1, block, data);

	for (i = 0; i < TWOFISH_XTS_BLOCK_LEN; i++)
		data[i] ^= iv[i];

	/* Exponentiate tweak */
	carry_in = 0;
	for (i = 0; i < TWOFISH_XTS_BLOCK_LEN; i++) {
		carry_out = iv[i] & 0x80;
		iv[i] = (iv[i] << 1) | (carry_in ? 1 : 0);
		carry_in = carry_out;
	}
	if (carry_in)
		iv[0] ^= AES_XTS_ALPHA;
	bzero(block, sizeof(block));
}

void
twofish_xts_encrypt(caddr_t key, u_int8_t *data, u_int8_t *iv)
{
	twofish_xts_crypt((struct twofish_xts_ctx *)key, data, iv, 1);
}

void
twofish_xts_decrypt(caddr_t key, u_int8_t *data, u_int8_t *iv)
{
	twofish_xts_crypt((struct twofish_xts_ctx *)key, data, iv, 0);
}

int
twofish_xts_setkey(void *sched, u_int8_t *key, int len)
{
	struct twofish_xts_ctx *ctx;

	if (len != 32 && len != 64)
		return -1;

	ctx = sched;
	twofish_set_key(&ctx->key1, key, len * 4);
	twofish_set_key(&ctx->key2, key + (len / 2), len * 4);

	return 0;
}


void
serpent_xts_reinit(caddr_t key, u_int8_t *iv)
{
	struct serpent_xts_ctx *ctx = (struct serpent_xts_ctx *)key;
#if 0
	u_int64_t blocknum;
	u_int i;
#endif

#if 0
	/*
	 * Prepare tweak as E_k2(IV). IV is specified as LE representation
	 * of a 64-bit block number which we allow to be passed in directly.
	 */
	/* XXX: possibly use htole64? */
#endif
	/* Last 64 bits of IV are always zero */
	bzero(iv + SERPENT_XTS_IV_LEN, SERPENT_XTS_IV_LEN);

	serpent_encrypt(&ctx->key2, iv, iv);
}

void
serpent_xts_crypt(struct serpent_xts_ctx *ctx, u_int8_t *data, u_int8_t *iv,
    u_int do_encrypt)
{
	u_int8_t block[SERPENT_XTS_BLOCK_LEN];
	u_int i, carry_in, carry_out;

	for (i = 0; i < SERPENT_XTS_BLOCK_LEN; i++)
		block[i] = data[i] ^ iv[i];

	if (do_encrypt)
		serpent_encrypt(&ctx->key1, block, data);
	else
		serpent_decrypt(&ctx->key1, block, data);

	for (i = 0; i < SERPENT_XTS_BLOCK_LEN; i++)
		data[i] ^= iv[i];

	/* Exponentiate tweak */
	carry_in = 0;
	for (i = 0; i < SERPENT_XTS_BLOCK_LEN; i++) {
		carry_out = iv[i] & 0x80;
		iv[i] = (iv[i] << 1) | (carry_in ? 1 : 0);
		carry_in = carry_out;
	}
	if (carry_in)
		iv[0] ^= AES_XTS_ALPHA;
	bzero(block, sizeof(block));
}

void
serpent_xts_encrypt(caddr_t key, u_int8_t *data, u_int8_t *iv)
{
	serpent_xts_crypt((struct serpent_xts_ctx *)key, data, iv, 1);
}

void
serpent_xts_decrypt(caddr_t key, u_int8_t *data, u_int8_t *iv)
{
	serpent_xts_crypt((struct serpent_xts_ctx *)key, data, iv, 0);
}

int
serpent_xts_setkey(void *sched, u_int8_t *key, int len)
{
	struct serpent_xts_ctx *ctx;

	if (len != 32 && len != 64)
		return -1;

	ctx = sched;
	serpent_set_key(&ctx->key1, key, len * 4);
	serpent_set_key(&ctx->key2, key + (len / 2), len * 4);

	return 0;
}


/*
 * And now for auth.
 */

static void
null_init(void *ctx)
{
}

static int
null_update(void *ctx, u_int8_t *buf, u_int16_t len)
{
	return 0;
}

static void
null_final(u_int8_t *buf, void *ctx)
{
	if (buf != NULL)
		bzero(buf, 12);
}

static int
RMD160Update_int(void *ctx, u_int8_t *buf, u_int16_t len)
{
	RMD160Update(ctx, buf, len);
	return 0;
}

static int
MD5Update_int(void *ctx, u_int8_t *buf, u_int16_t len)
{
	MD5Update(ctx, buf, len);
	return 0;
}

static void
SHA1Init_int(void *ctx)
{
	SHA1Init(ctx);
}

static int
SHA1Update_int(void *ctx, u_int8_t *buf, u_int16_t len)
{
	SHA1Update(ctx, buf, len);
	return 0;
}

static void
SHA1Final_int(u_int8_t *blk, void *ctx)
{
	SHA1Final(blk, ctx);
}

static int
SHA256Update_int(void *ctx, u_int8_t *buf, u_int16_t len)
{
	SHA256_Update(ctx, buf, len);
	return 0;
}

static int
SHA384Update_int(void *ctx, u_int8_t *buf, u_int16_t len)
{
	SHA384_Update(ctx, buf, len);
	return 0;
}

static int
SHA512Update_int(void *ctx, u_int8_t *buf, u_int16_t len)
{
	SHA512_Update(ctx, buf, len);
	return 0;
}

/*
 * And compression
 */

static u_int32_t
deflate_compress(u_int8_t *data, u_int32_t size, u_int8_t **out)
{
	return deflate_global(data, size, 0, out);
}

static u_int32_t
deflate_decompress(u_int8_t *data, u_int32_t size, u_int8_t **out)
{
	return deflate_global(data, size, 1, out);
}
