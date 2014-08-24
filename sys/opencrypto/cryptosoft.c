/*-
 * The author of this code is Angelos D. Keromytis (angelos@cis.upenn.edu)
 * Copyright (c) 2002-2006 Sam Leffler, Errno Consulting
 *
 * This code was written by Angelos D. Keromytis in Athens, Greece, in
 * February 2000. Network Security Technologies Inc. (NSTI) kindly
 * supported the development of this code.
 *
 * Copyright (c) 2000, 2001 Angelos D. Keromytis
 *
 * SMP modifications by Matthew Dillon for the DragonFlyBSD Project
 *
 * Permission to use, copy, and modify this software with or without fee
 * is hereby granted, provided that this entire notice is included in
 * all source code copies of any software which is or includes a copy or
 * modification of this software.
 *
 * THIS SOFTWARE IS BEING PROVIDED "AS IS", WITHOUT ANY EXPRESS OR
 * IMPLIED WARRANTY. IN PARTICULAR, NONE OF THE AUTHORS MAKES ANY
 * REPRESENTATION OR WARRANTY OF ANY KIND CONCERNING THE
 * MERCHANTABILITY OF THIS SOFTWARE OR ITS FITNESS FOR ANY PARTICULAR
 * PURPOSE.
 *
 * $FreeBSD: src/sys/opencrypto/cryptosoft.c,v 1.23 2009/02/05 17:43:12 imp Exp $
 * $OpenBSD: cryptosoft.c,v 1.35 2002/04/26 08:43:50 deraadt Exp $
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/malloc.h>
#include <sys/mbuf.h>
#include <sys/module.h>
#include <sys/sysctl.h>
#include <sys/errno.h>
#include <sys/endian.h>
#include <sys/random.h>
#include <sys/kernel.h>
#include <sys/uio.h>
#include <sys/spinlock2.h>

#include <crypto/blowfish/blowfish.h>
#include <crypto/sha1.h>
#include <opencrypto/rmd160.h>
#include <opencrypto/cast.h>
#include <opencrypto/skipjack.h>
#include <sys/md5.h>

#include <opencrypto/cryptodev.h>
#include <opencrypto/cryptosoft.h>
#include <opencrypto/xform.h>

#include <sys/kobj.h>
#include <sys/bus.h>
#include "cryptodev_if.h"

static	int32_t swcr_id;
static	struct swcr_data **swcr_sessions = NULL;
static	u_int32_t swcr_sesnum;
static	u_int32_t swcr_minsesnum = 1;

static struct spinlock swcr_spin = SPINLOCK_INITIALIZER(swcr_spin, "swcr_spin");

u_int8_t hmac_ipad_buffer[HMAC_MAX_BLOCK_LEN];
u_int8_t hmac_opad_buffer[HMAC_MAX_BLOCK_LEN];

static	int swcr_encdec(struct cryptodesc *, struct swcr_data *, caddr_t, int);
static	int swcr_authcompute(struct cryptodesc *, struct swcr_data *, caddr_t, int);
static	int swcr_combined(struct cryptop *);
static	int swcr_compdec(struct cryptodesc *, struct swcr_data *, caddr_t, int);
static	int swcr_freesession(device_t dev, u_int64_t tid);
static	int swcr_freesession_slot(struct swcr_data **swdp, u_int32_t sid);

/*
 * Apply a symmetric encryption/decryption algorithm.
 */
static int
swcr_encdec(struct cryptodesc *crd, struct swcr_data *sw, caddr_t buf,
    int flags)
{
	unsigned char iv[EALG_MAX_BLOCK_LEN], blk[EALG_MAX_BLOCK_LEN], *idat;
	unsigned char *ivp, *nivp, iv2[EALG_MAX_BLOCK_LEN];
	u_int8_t *kschedule;
	u_int8_t *okschedule;
	struct enc_xform *exf;
	int i, k, j, blks, ivlen;
	int error;
	int explicit_kschedule;

	exf = sw->sw_exf;
	blks = exf->blocksize;
	ivlen = exf->ivsize;

	/* Check for non-padded data */
	if (crd->crd_len % blks)
		return EINVAL;

	/* Initialize the IV */
	if (crd->crd_flags & CRD_F_ENCRYPT) {
		/* IV explicitly provided ? */
		if (crd->crd_flags & CRD_F_IV_EXPLICIT)
			bcopy(crd->crd_iv, iv, ivlen);
		else
			karc4rand(iv, ivlen);

		/* Do we need to write the IV */
		if (!(crd->crd_flags & CRD_F_IV_PRESENT))
			crypto_copyback(flags, buf, crd->crd_inject, ivlen, iv);

	} else {	/* Decryption */
			/* IV explicitly provided ? */
		if (crd->crd_flags & CRD_F_IV_EXPLICIT)
			bcopy(crd->crd_iv, iv, ivlen);
		else {
			/* Get IV off buf */
			crypto_copydata(flags, buf, crd->crd_inject, ivlen, iv);
		}
	}

	ivp = iv;

	/*
	 * The semantics are seriously broken because the session key
	 * storage was never designed for concurrent ops.
	 */
	if (crd->crd_flags & CRD_F_KEY_EXPLICIT) {
		kschedule = NULL;
		explicit_kschedule = 1;
		error = exf->setkey(&kschedule,
				    crd->crd_key, crd->crd_klen / 8);
		if (error)
			goto done;
	} else {
		spin_lock(&swcr_spin);
		kschedule = sw->sw_kschedule;
		++sw->sw_kschedule_refs;
		spin_unlock(&swcr_spin);
		explicit_kschedule = 0;
	}

	/*
	 * xforms that provide a reinit method perform all IV
	 * handling themselves.
	 */
	if (exf->reinit)
		exf->reinit(kschedule, iv);

	if (flags & CRYPTO_F_IMBUF) {
		struct mbuf *m = (struct mbuf *) buf;

		/* Find beginning of data */
		m = m_getptr(m, crd->crd_skip, &k);
		if (m == NULL) {
			error = EINVAL;
			goto done;
		}

		i = crd->crd_len;

		while (i > 0) {
			/*
			 * If there's insufficient data at the end of
			 * an mbuf, we have to do some copying.
			 */
			if (m->m_len < k + blks && m->m_len != k) {
				m_copydata(m, k, blks, blk);

				/* Actual encryption/decryption */
				if (exf->reinit) {
					if (crd->crd_flags & CRD_F_ENCRYPT) {
						exf->encrypt(kschedule,
						    blk, iv);
					} else {
						exf->decrypt(kschedule,
						    blk, iv);
					}
				} else if (crd->crd_flags & CRD_F_ENCRYPT) {
					/* XOR with previous block */
					for (j = 0; j < blks; j++)
						blk[j] ^= ivp[j];

					exf->encrypt(kschedule, blk, iv);

					/*
					 * Keep encrypted block for XOR'ing
					 * with next block
					 */
					bcopy(blk, iv, blks);
					ivp = iv;
				} else {	/* decrypt */
					/*	
					 * Keep encrypted block for XOR'ing
					 * with next block
					 */
					nivp = (ivp == iv) ? iv2 : iv;
					bcopy(blk, nivp, blks);

					exf->decrypt(kschedule, blk, iv);

					/* XOR with previous block */
					for (j = 0; j < blks; j++)
						blk[j] ^= ivp[j];

					ivp = nivp;
				}

				/* Copy back decrypted block */
				m_copyback(m, k, blks, blk);

				/* Advance pointer */
				m = m_getptr(m, k + blks, &k);
				if (m == NULL) {
					error = EINVAL;
					goto done;
				}

				i -= blks;

				/* Could be done... */
				if (i == 0)
					break;
			}

			/* Skip possibly empty mbufs */
			if (k == m->m_len) {
				for (m = m->m_next; m && m->m_len == 0;
				    m = m->m_next)
					;
				k = 0;
			}

			/* Sanity check */
			if (m == NULL) {
				error = EINVAL;
				goto done;
			}

			/*
			 * Warning: idat may point to garbage here, but
			 * we only use it in the while() loop, only if
			 * there are indeed enough data.
			 */
			idat = mtod(m, unsigned char *) + k;

	   		while (m->m_len >= k + blks && i > 0) {
				if (exf->reinit) {
					if (crd->crd_flags & CRD_F_ENCRYPT) {
						exf->encrypt(kschedule,
						    idat, iv);
					} else {
						exf->decrypt(kschedule,
						    idat, iv);
					}
				} else if (crd->crd_flags & CRD_F_ENCRYPT) {
					/* XOR with previous block/IV */
					for (j = 0; j < blks; j++)
						idat[j] ^= ivp[j];

					exf->encrypt(kschedule, idat, iv);
					ivp = idat;
				} else {	/* decrypt */
					/*
					 * Keep encrypted block to be used
					 * in next block's processing.
					 */
					nivp = (ivp == iv) ? iv2 : iv;
					bcopy(idat, nivp, blks);

					exf->decrypt(kschedule, idat, iv);

					/* XOR with previous block/IV */
					for (j = 0; j < blks; j++)
						idat[j] ^= ivp[j];

					ivp = nivp;
				}

				idat += blks;
				k += blks;
				i -= blks;
			}
		}
		error = 0;	/* Done with mbuf encryption/decryption */
	} else if (flags & CRYPTO_F_IOV) {
		struct uio *uio = (struct uio *) buf;
		struct iovec *iov;

		/* Find beginning of data */
		iov = cuio_getptr(uio, crd->crd_skip, &k);
		if (iov == NULL) {
			error = EINVAL;
			goto done;
		}

		i = crd->crd_len;

		while (i > 0) {
			/*
			 * If there's insufficient data at the end of
			 * an iovec, we have to do some copying.
			 */
			if (iov->iov_len < k + blks && iov->iov_len != k) {
				cuio_copydata(uio, k, blks, blk);

				/* Actual encryption/decryption */
				if (exf->reinit) {
					if (crd->crd_flags & CRD_F_ENCRYPT) {
						exf->encrypt(kschedule,
						    blk, iv);
					} else {
						exf->decrypt(kschedule,
						    blk, iv);
					}
				} else if (crd->crd_flags & CRD_F_ENCRYPT) {
					/* XOR with previous block */
					for (j = 0; j < blks; j++)
						blk[j] ^= ivp[j];

					exf->encrypt(kschedule, blk, iv);

					/*
					 * Keep encrypted block for XOR'ing
					 * with next block
					 */
					bcopy(blk, iv, blks);
					ivp = iv;
				} else {	/* decrypt */
					/*	
					 * Keep encrypted block for XOR'ing
					 * with next block
					 */
					nivp = (ivp == iv) ? iv2 : iv;
					bcopy(blk, nivp, blks);

					exf->decrypt(kschedule, blk, iv);

					/* XOR with previous block */
					for (j = 0; j < blks; j++)
						blk[j] ^= ivp[j];

					ivp = nivp;
				}

				/* Copy back decrypted block */
				cuio_copyback(uio, k, blks, blk);

				/* Advance pointer */
				iov = cuio_getptr(uio, k + blks, &k);
				if (iov == NULL) {
					error = EINVAL;
					goto done;
				}

				i -= blks;

				/* Could be done... */
				if (i == 0)
					break;
			}

			/*
			 * Warning: idat may point to garbage here, but
			 * we only use it in the while() loop, only if
			 * there are indeed enough data.
			 */
			idat = (char *)iov->iov_base + k;

	   		while (iov->iov_len >= k + blks && i > 0) {
				if (exf->reinit) {
					if (crd->crd_flags & CRD_F_ENCRYPT) {
						exf->encrypt(kschedule,
						    idat, iv);
					} else {
						exf->decrypt(kschedule,
						    idat, iv);
					}
				} else if (crd->crd_flags & CRD_F_ENCRYPT) {
					/* XOR with previous block/IV */
					for (j = 0; j < blks; j++)
						idat[j] ^= ivp[j];

					exf->encrypt(kschedule, idat, iv);
					ivp = idat;
				} else {	/* decrypt */
					/*
					 * Keep encrypted block to be used
					 * in next block's processing.
					 */
					nivp = (ivp == iv) ? iv2 : iv;
					bcopy(idat, nivp, blks);

					exf->decrypt(kschedule, idat, iv);

					/* XOR with previous block/IV */
					for (j = 0; j < blks; j++)
						idat[j] ^= ivp[j];

					ivp = nivp;
				}

				idat += blks;
				k += blks;
				i -= blks;
			}
			if (k == iov->iov_len) {
				iov++;
				k = 0;
			}
		}
		error = 0;	/* Done with iovec encryption/decryption */
	} else {
		/*
		 * contiguous buffer
		 */
		if (exf->reinit) {
			for(i = crd->crd_skip;
			    i < crd->crd_skip + crd->crd_len; i += blks) {
				if (crd->crd_flags & CRD_F_ENCRYPT) {
					exf->encrypt(kschedule, buf + i, iv);
				} else {
					exf->decrypt(kschedule, buf + i, iv);
				}
			}
		} else if (crd->crd_flags & CRD_F_ENCRYPT) {
			for (i = crd->crd_skip;
			    i < crd->crd_skip + crd->crd_len; i += blks) {
				/* XOR with the IV/previous block, as appropriate. */
				if (i == crd->crd_skip)
					for (k = 0; k < blks; k++)
						buf[i + k] ^= ivp[k];
				else
					for (k = 0; k < blks; k++)
						buf[i + k] ^= buf[i + k - blks];
				exf->encrypt(kschedule, buf + i, iv);
			}
		} else {		/* Decrypt */
			/*
			 * Start at the end, so we don't need to keep the
			 * encrypted block as the IV for the next block.
			 */
			for (i = crd->crd_skip + crd->crd_len - blks;
			    i >= crd->crd_skip; i -= blks) {
				exf->decrypt(kschedule, buf + i, iv);

				/* XOR with the IV/previous block, as appropriate */
				if (i == crd->crd_skip)
					for (k = 0; k < blks; k++)
						buf[i + k] ^= ivp[k];
				else
					for (k = 0; k < blks; k++)
						buf[i + k] ^= buf[i + k - blks];
			}
		}
		error = 0; /* Done w/contiguous buffer encrypt/decrypt */
	}
done:
	/*
	 * Cleanup - explicitly replace the session key if requested
	 *	     (horrible semantics for concurrent operation)
	 */
	if (explicit_kschedule) {
		spin_lock(&swcr_spin);
		if (sw->sw_kschedule && sw->sw_kschedule_refs == 0) {
			okschedule = sw->sw_kschedule;
			sw->sw_kschedule = kschedule;
		} else {
			okschedule = NULL;
		}
		spin_unlock(&swcr_spin);
		if (okschedule)
			exf->zerokey(&okschedule);
	} else {
		spin_lock(&swcr_spin);
		--sw->sw_kschedule_refs;
		spin_unlock(&swcr_spin);
	}
	return error;
}

static void
swcr_authprepare(struct auth_hash *axf, struct swcr_data *sw, u_char *key,
    int klen)
{
	int k;

	klen /= 8;

	switch (axf->type) {
	case CRYPTO_MD5_HMAC:
	case CRYPTO_SHA1_HMAC:
	case CRYPTO_SHA2_256_HMAC:
	case CRYPTO_SHA2_384_HMAC:
	case CRYPTO_SHA2_512_HMAC:
	case CRYPTO_NULL_HMAC:
	case CRYPTO_RIPEMD160_HMAC:
		for (k = 0; k < klen; k++)
			key[k] ^= HMAC_IPAD_VAL;

		axf->Init(sw->sw_ictx);
		axf->Update(sw->sw_ictx, key, klen);
		axf->Update(sw->sw_ictx, hmac_ipad_buffer, axf->blocksize - klen);

		for (k = 0; k < klen; k++)
			key[k] ^= (HMAC_IPAD_VAL ^ HMAC_OPAD_VAL);

		axf->Init(sw->sw_octx);
		axf->Update(sw->sw_octx, key, klen);
		axf->Update(sw->sw_octx, hmac_opad_buffer, axf->blocksize - klen);

		for (k = 0; k < klen; k++)
			key[k] ^= HMAC_OPAD_VAL;
		break;
	case CRYPTO_MD5_KPDK:
	case CRYPTO_SHA1_KPDK:
	{
		/* We need a buffer that can hold an md5 and a sha1 result. */
		u_char buf[SHA1_RESULTLEN];

		sw->sw_klen = klen;
		bcopy(key, sw->sw_octx, klen);
		axf->Init(sw->sw_ictx);
		axf->Update(sw->sw_ictx, key, klen);
		axf->Final(buf, sw->sw_ictx);
		break;
	}
	default:
		kprintf("%s: CRD_F_KEY_EXPLICIT flag given, but algorithm %d "
		    "doesn't use keys.\n", __func__, axf->type);
	}
}

/*
 * Compute keyed-hash authenticator.
 */
static int
swcr_authcompute(struct cryptodesc *crd, struct swcr_data *sw, caddr_t buf,
    int flags)
{
	unsigned char aalg[HASH_MAX_LEN];
	struct auth_hash *axf;
	union authctx ctx;
	int err;

	if (sw->sw_ictx == NULL)
		return EINVAL;

	axf = sw->sw_axf;

	if (crd->crd_flags & CRD_F_KEY_EXPLICIT)
		swcr_authprepare(axf, sw, crd->crd_key, crd->crd_klen);

	bcopy(sw->sw_ictx, &ctx, axf->ctxsize);

	err = crypto_apply(flags, buf, crd->crd_skip, crd->crd_len,
	    (int (*)(void *, void *, unsigned int))axf->Update, (caddr_t)&ctx);
	if (err)
		return err;

	switch (sw->sw_alg) {
	case CRYPTO_MD5_HMAC:
	case CRYPTO_SHA1_HMAC:
	case CRYPTO_SHA2_256_HMAC:
	case CRYPTO_SHA2_384_HMAC:
	case CRYPTO_SHA2_512_HMAC:
	case CRYPTO_RIPEMD160_HMAC:
		if (sw->sw_octx == NULL)
			return EINVAL;

		axf->Final(aalg, &ctx);
		bcopy(sw->sw_octx, &ctx, axf->ctxsize);
		axf->Update(&ctx, aalg, axf->hashsize);
		axf->Final(aalg, &ctx);
		break;

	case CRYPTO_MD5_KPDK:
	case CRYPTO_SHA1_KPDK:
		if (sw->sw_octx == NULL)
			return EINVAL;

		axf->Update(&ctx, sw->sw_octx, sw->sw_klen);
		axf->Final(aalg, &ctx);
		break;

	case CRYPTO_NULL_HMAC:
		axf->Final(aalg, &ctx);
		break;
	}

	/* Inject the authentication data */
	crypto_copyback(flags, buf, crd->crd_inject,
	    sw->sw_mlen == 0 ? axf->hashsize : sw->sw_mlen, aalg);
	return 0;
}

/*
 * Apply a combined encryption-authentication transformation
 */
static int
swcr_combined(struct cryptop *crp)
{
	uint32_t blkbuf[howmany(EALG_MAX_BLOCK_LEN, sizeof(uint32_t))];
	u_char *blk = (u_char *)blkbuf;
	u_char aalg[HASH_MAX_LEN];
	u_char iv[EALG_MAX_BLOCK_LEN];
	uint8_t *kschedule;
	union authctx ctx;
	struct cryptodesc *crd, *crda = NULL, *crde = NULL;
	struct swcr_data *sw, *swa, *swe;
	struct auth_hash *axf = NULL;
	struct enc_xform *exf = NULL;
	caddr_t buf = (caddr_t)crp->crp_buf;
	uint32_t *blkp;
	int i, blksz, ivlen, len;

	blksz = 0;
	ivlen = 0;

	for (crd = crp->crp_desc; crd; crd = crd->crd_next) {
		for (sw = swcr_sessions[crp->crp_sid & 0xffffffff];
		     sw && sw->sw_alg != crd->crd_alg;
		     sw = sw->sw_next)
			;
		if (sw == NULL)
			return (EINVAL);

		switch (sw->sw_alg) {
		case CRYPTO_AES_GCM_16:
		case CRYPTO_AES_GMAC:
			swe = sw;
			crde = crd;
			exf = swe->sw_exf;
			ivlen = exf->ivsize;
			break;
		case CRYPTO_AES_128_GMAC:
		case CRYPTO_AES_192_GMAC:
		case CRYPTO_AES_256_GMAC:
			swa = sw;
			crda = crd;
			axf = swa->sw_axf;
			if (swa->sw_ictx == NULL)
				return (EINVAL);
			bcopy(swa->sw_ictx, &ctx, axf->ctxsize);
			blksz = axf->blocksize;
			break;
		default:
			return (EINVAL);
		}
	}
	if (crde == NULL || crda == NULL)
		return (EINVAL);

	/* Initialize the IV */
	if (crde->crd_flags & CRD_F_ENCRYPT) {
		/* IV explicitly provided ? */
		if (crde->crd_flags & CRD_F_IV_EXPLICIT)
			bcopy(crde->crd_iv, iv, ivlen);
		else
			karc4rand(iv, ivlen);

		/* Do we need to write the IV */
		if (!(crde->crd_flags & CRD_F_IV_PRESENT))
			crypto_copyback(crde->crd_flags, buf, crde->crd_inject,
			    ivlen, iv);

	} else {	/* Decryption */
			/* IV explicitly provided ? */
		if (crde->crd_flags & CRD_F_IV_EXPLICIT)
			bcopy(crde->crd_iv, iv, ivlen);
		else
			/* Get IV off buf */
			crypto_copydata(crde->crd_flags, buf, crde->crd_inject,
			    ivlen, iv);
	}

	/* Supply MAC with IV */
	if (axf->Reinit)
		axf->Reinit(&ctx, iv, ivlen);

	/* Supply MAC with AAD */
	for (i = 0; i < crda->crd_len; i += blksz) {
		len = MIN(crda->crd_len - i, blksz);
		crypto_copydata(crde->crd_flags, buf, crda->crd_skip + i, len,
		    blk);
		axf->Update(&ctx, blk, len);
	}

	spin_lock(&swcr_spin);
	kschedule = sw->sw_kschedule;
	++sw->sw_kschedule_refs;
	spin_unlock(&swcr_spin);

	if (exf->reinit)
		exf->reinit(kschedule, iv);

	/* Do encryption/decryption with MAC */
	for (i = 0; i < crde->crd_len; i += blksz) {
		len = MIN(crde->crd_len - i, blksz);
		if (len < blksz)
			bzero(blk, blksz);
		crypto_copydata(crde->crd_flags, buf, crde->crd_skip + i, len,
		    blk);
		if (crde->crd_flags & CRD_F_ENCRYPT) {
			exf->encrypt(kschedule, blk, iv);
			axf->Update(&ctx, blk, len);
		} else {
			axf->Update(&ctx, blk, len);
			exf->decrypt(kschedule, blk, iv);
		}
		crypto_copyback(crde->crd_flags, buf, crde->crd_skip + i, len,
		    blk);
	}

	/* Do any required special finalization */
	switch (crda->crd_alg) {
		case CRYPTO_AES_128_GMAC:
		case CRYPTO_AES_192_GMAC:
		case CRYPTO_AES_256_GMAC:
			/* length block */
			bzero(blk, blksz);
			blkp = (uint32_t *)blk + 1;
			*blkp = htobe32(crda->crd_len * 8);
			blkp = (uint32_t *)blk + 3;
			*blkp = htobe32(crde->crd_len * 8);
			axf->Update(&ctx, blk, blksz);
			break;
	}

	/* Finalize MAC */
	axf->Final(aalg, &ctx);

	/* Inject the authentication data */
	crypto_copyback(crda->crd_flags, crp->crp_buf, crda->crd_inject,
	    axf->blocksize, aalg);

	spin_lock(&swcr_spin);
	--sw->sw_kschedule_refs;
	spin_unlock(&swcr_spin);

	return (0);
}

/*
 * Apply a compression/decompression algorithm
 */
static int
swcr_compdec(struct cryptodesc *crd, struct swcr_data *sw,
	     caddr_t buf, int flags)
{
	u_int8_t *data, *out;
	struct comp_algo *cxf;
	int adj;
	u_int32_t result;

	cxf = sw->sw_cxf;

	/*
	 * We must handle the whole buffer of data in one time
	 * then if there is not all the data in the mbuf, we must
	 * copy in a buffer.
	 */
	data = kmalloc(crd->crd_len, M_CRYPTO_DATA, M_INTWAIT);
	crypto_copydata(flags, buf, crd->crd_skip, crd->crd_len, data);

	if (crd->crd_flags & CRD_F_COMP)
		result = cxf->compress(data, crd->crd_len, &out);
	else
		result = cxf->decompress(data, crd->crd_len, &out);

	kfree(data, M_CRYPTO_DATA);
	if (result == 0)
		return EINVAL;

	/* Copy back the (de)compressed data. m_copyback is
	 * extending the mbuf as necessary.
	 */
	sw->sw_size = result;
	/* Check the compressed size when doing compression */
	if (crd->crd_flags & CRD_F_COMP) {
		if (result >= crd->crd_len) {
			/* Compression was useless, we lost time */
			kfree(out, M_CRYPTO_DATA);
			return 0;
		}
	}

	crypto_copyback(flags, buf, crd->crd_skip, result, out);
	if (result < crd->crd_len) {
		adj = result - crd->crd_len;
		if (flags & CRYPTO_F_IMBUF) {
			adj = result - crd->crd_len;
			m_adj((struct mbuf *)buf, adj);
		} else if (flags & CRYPTO_F_IOV) {
			struct uio *uio = (struct uio *)buf;
			int ind;

			adj = crd->crd_len - result;
			ind = uio->uio_iovcnt - 1;

			while (adj > 0 && ind >= 0) {
				if (adj < uio->uio_iov[ind].iov_len) {
					uio->uio_iov[ind].iov_len -= adj;
					break;
				}

				adj -= uio->uio_iov[ind].iov_len;
				uio->uio_iov[ind].iov_len = 0;
				ind--;
				uio->uio_iovcnt--;
			}
		}
	}
	kfree(out, M_CRYPTO_DATA);
	return 0;
}

/*
 * Generate a new software session.
 */
static int
swcr_newsession(device_t dev, u_int32_t *sid, struct cryptoini *cri)
{
	struct swcr_data *swd_base;
	struct swcr_data **swd;
	struct swcr_data **oswd;
	struct auth_hash *axf;
	struct enc_xform *txf;
	struct comp_algo *cxf;
	u_int32_t i;
	u_int32_t n;
	int error;

	if (sid == NULL || cri == NULL)
		return EINVAL;

	swd_base = NULL;
	swd = &swd_base;

	while (cri) {
		*swd = kmalloc(sizeof(struct swcr_data),
			       M_CRYPTO_DATA, M_WAITOK | M_ZERO);

		switch (cri->cri_alg) {
		case CRYPTO_DES_CBC:
			txf = &enc_xform_des;
			goto enccommon;
		case CRYPTO_3DES_CBC:
			txf = &enc_xform_3des;
			goto enccommon;
		case CRYPTO_BLF_CBC:
			txf = &enc_xform_blf;
			goto enccommon;
		case CRYPTO_CAST_CBC:
			txf = &enc_xform_cast5;
			goto enccommon;
		case CRYPTO_SKIPJACK_CBC:
			txf = &enc_xform_skipjack;
			goto enccommon;
		case CRYPTO_RIJNDAEL128_CBC:
			txf = &enc_xform_rijndael128;
			goto enccommon;
		case CRYPTO_AES_XTS:
			txf = &enc_xform_aes_xts;
			goto enccommon;
		case CRYPTO_AES_CTR:
			txf = &enc_xform_aes_ctr;
			goto enccommon;
		case CRYPTO_AES_GCM_16:
			txf = &enc_xform_aes_gcm;
			goto enccommon;
		case CRYPTO_AES_GMAC:
			txf = &enc_xform_aes_gmac;
			(*swd)->sw_exf = txf;
			break;
		case CRYPTO_CAMELLIA_CBC:
			txf = &enc_xform_camellia;
			goto enccommon;
		case CRYPTO_TWOFISH_CBC:
			txf = &enc_xform_twofish;
			goto enccommon;
		case CRYPTO_SERPENT_CBC:
			txf = &enc_xform_serpent;
			goto enccommon;
		case CRYPTO_TWOFISH_XTS:
			txf = &enc_xform_twofish_xts;
			goto enccommon;
		case CRYPTO_SERPENT_XTS:
			txf = &enc_xform_serpent_xts;
			goto enccommon;
		case CRYPTO_NULL_CBC:
			txf = &enc_xform_null;
			goto enccommon;
		enccommon:
			if (cri->cri_key != NULL) {
				error = txf->setkey(&((*swd)->sw_kschedule),
						    cri->cri_key,
						    cri->cri_klen / 8);
				if (error) {
					swcr_freesession_slot(&swd_base, 0);
					return error;
				}
			}
			(*swd)->sw_exf = txf;
			break;

		case CRYPTO_MD5_HMAC:
			axf = &auth_hash_hmac_md5;
			goto authcommon;
		case CRYPTO_SHA1_HMAC:
			axf = &auth_hash_hmac_sha1;
			goto authcommon;
		case CRYPTO_SHA2_256_HMAC:
			axf = &auth_hash_hmac_sha2_256;
			goto authcommon;
		case CRYPTO_SHA2_384_HMAC:
			axf = &auth_hash_hmac_sha2_384;
			goto authcommon;
		case CRYPTO_SHA2_512_HMAC:
			axf = &auth_hash_hmac_sha2_512;
			goto authcommon;
		case CRYPTO_NULL_HMAC:
			axf = &auth_hash_null;
			goto authcommon;
		case CRYPTO_RIPEMD160_HMAC:
			axf = &auth_hash_hmac_ripemd_160;
		authcommon:
			(*swd)->sw_ictx = kmalloc(axf->ctxsize, M_CRYPTO_DATA,
						  M_WAITOK);
	
			(*swd)->sw_octx = kmalloc(axf->ctxsize, M_CRYPTO_DATA,
						  M_WAITOK);
	
			if (cri->cri_key != NULL) {
				swcr_authprepare(axf, *swd, cri->cri_key,
				    cri->cri_klen);
			}
	
			(*swd)->sw_mlen = cri->cri_mlen;
			(*swd)->sw_axf = axf;
			break;
	
		case CRYPTO_MD5_KPDK:
			axf = &auth_hash_key_md5;
			goto auth2common;
	
		case CRYPTO_SHA1_KPDK:
			axf = &auth_hash_key_sha1;
		auth2common:
			(*swd)->sw_ictx = kmalloc(axf->ctxsize, M_CRYPTO_DATA,
						  M_WAITOK);
	
			(*swd)->sw_octx = kmalloc(cri->cri_klen / 8,
						  M_CRYPTO_DATA, M_WAITOK);
	
			/* Store the key so we can "append" it to the payload */
			if (cri->cri_key != NULL) {
				swcr_authprepare(axf, *swd, cri->cri_key,
				    cri->cri_klen);
			}

			(*swd)->sw_mlen = cri->cri_mlen;
			(*swd)->sw_axf = axf;
			break;
#ifdef notdef
		case CRYPTO_MD5:
			axf = &auth_hash_md5;
			goto auth3common;

		case CRYPTO_SHA1:
			axf = &auth_hash_sha1;
		auth3common:
			(*swd)->sw_ictx = kmalloc(axf->ctxsize, M_CRYPTO_DATA,
						  M_WAITOK);

			axf->Init((*swd)->sw_ictx);
			(*swd)->sw_mlen = cri->cri_mlen;
			(*swd)->sw_axf = axf;
			break;
#endif
		case CRYPTO_AES_128_GMAC:
			axf = &auth_hash_gmac_aes_128;
			goto auth4common;

		case CRYPTO_AES_192_GMAC:
			axf = &auth_hash_gmac_aes_192;
			goto auth4common;

		case CRYPTO_AES_256_GMAC:
			axf = &auth_hash_gmac_aes_256;
		auth4common:
			(*swd)->sw_ictx = kmalloc(axf->ctxsize, M_CRYPTO_DATA,
			    M_NOWAIT);
			if ((*swd)->sw_ictx == NULL) {
				swcr_freesession_slot(&swd_base, 0);
				return ENOBUFS;
			}

			axf->Init((*swd)->sw_ictx);
			axf->Setkey((*swd)->sw_ictx, cri->cri_key,
			    cri->cri_klen / 8);
			(*swd)->sw_axf = axf;
			break;

		case CRYPTO_DEFLATE_COMP:
			cxf = &comp_algo_deflate;
			(*swd)->sw_cxf = cxf;
			break;
		default:
			swcr_freesession_slot(&swd_base, 0);
			return EINVAL;
		}
	
		(*swd)->sw_alg = cri->cri_alg;
		cri = cri->cri_next;
		swd = &((*swd)->sw_next);
	}

	for (;;) {
		/*
		 * Atomically allocate a session
		 */
		spin_lock(&swcr_spin);
		for (i = swcr_minsesnum; i < swcr_sesnum; ++i) {
			if (swcr_sessions[i] == NULL)
				break;
		}
		if (i < swcr_sesnum) {
			swcr_sessions[i] = swd_base;
			swcr_minsesnum = i + 1;
			spin_unlock(&swcr_spin);
			break;
		}
		n = swcr_sesnum;
		spin_unlock(&swcr_spin);

		/*
		 * A larger allocation is required, reallocate the array
		 * and replace, checking for SMP races.
		 */
		if (n < CRYPTO_SW_SESSIONS)
			n = CRYPTO_SW_SESSIONS;
		else
			n = n * 3 / 2;
		swd = kmalloc(n * sizeof(struct swcr_data *),
			      M_CRYPTO_DATA, M_WAITOK | M_ZERO);

		spin_lock(&swcr_spin);
		if (swcr_sesnum >= n) {
			spin_unlock(&swcr_spin);
			kfree(swd, M_CRYPTO_DATA);
		} else if (swcr_sesnum) {
			bcopy(swcr_sessions, swd,
			      swcr_sesnum * sizeof(struct swcr_data *));
			oswd = swcr_sessions;
			swcr_sessions = swd;
			swcr_sesnum = n;
			spin_unlock(&swcr_spin);
			kfree(oswd, M_CRYPTO_DATA);
		} else {
			swcr_sessions = swd;
			swcr_sesnum = n;
			spin_unlock(&swcr_spin);
		}
	}

	*sid = i;
	return 0;
}

/*
 * Free a session.
 */
static int
swcr_freesession(device_t dev, u_int64_t tid)
{
	u_int32_t sid = CRYPTO_SESID2LID(tid);

	if (sid > swcr_sesnum || swcr_sessions == NULL ||
	    swcr_sessions[sid] == NULL) {
		return EINVAL;
	}

	/* Silently accept and return */
	if (sid == 0)
		return 0;

	return(swcr_freesession_slot(&swcr_sessions[sid], sid));
}

static
int
swcr_freesession_slot(struct swcr_data **swdp, u_int32_t sid)
{
	struct enc_xform *txf;
	struct auth_hash *axf;
	struct swcr_data *swd;
	struct swcr_data *swnext;

	/*
	 * Protect session detachment with the spinlock.
	 */
	spin_lock(&swcr_spin);
	swnext = *swdp;
	*swdp = NULL;
	if (sid && swcr_minsesnum > sid)
		swcr_minsesnum = sid;
	spin_unlock(&swcr_spin);

	/*
	 * Clean up at our leisure.
	 */
	while ((swd = swnext) != NULL) {
		swnext = swd->sw_next;

		swd->sw_next = NULL;

		switch (swd->sw_alg) {
		case CRYPTO_DES_CBC:
		case CRYPTO_3DES_CBC:
		case CRYPTO_BLF_CBC:
		case CRYPTO_CAST_CBC:
		case CRYPTO_SKIPJACK_CBC:
		case CRYPTO_RIJNDAEL128_CBC:
		case CRYPTO_AES_XTS:
		case CRYPTO_AES_CTR:
		case CRYPTO_AES_GCM_16:
		case CRYPTO_AES_GMAC:
		case CRYPTO_CAMELLIA_CBC:
		case CRYPTO_TWOFISH_CBC:
		case CRYPTO_SERPENT_CBC:
		case CRYPTO_TWOFISH_XTS:
		case CRYPTO_SERPENT_XTS:
		case CRYPTO_NULL_CBC:
			txf = swd->sw_exf;

			if (swd->sw_kschedule)
				txf->zerokey(&(swd->sw_kschedule));
			break;

		case CRYPTO_MD5_HMAC:
		case CRYPTO_SHA1_HMAC:
		case CRYPTO_SHA2_256_HMAC:
		case CRYPTO_SHA2_384_HMAC:
		case CRYPTO_SHA2_512_HMAC:
		case CRYPTO_RIPEMD160_HMAC:
		case CRYPTO_NULL_HMAC:
			axf = swd->sw_axf;

			if (swd->sw_ictx) {
				bzero(swd->sw_ictx, axf->ctxsize);
				kfree(swd->sw_ictx, M_CRYPTO_DATA);
			}
			if (swd->sw_octx) {
				bzero(swd->sw_octx, axf->ctxsize);
				kfree(swd->sw_octx, M_CRYPTO_DATA);
			}
			break;

		case CRYPTO_MD5_KPDK:
		case CRYPTO_SHA1_KPDK:
			axf = swd->sw_axf;

			if (swd->sw_ictx) {
				bzero(swd->sw_ictx, axf->ctxsize);
				kfree(swd->sw_ictx, M_CRYPTO_DATA);
			}
			if (swd->sw_octx) {
				bzero(swd->sw_octx, swd->sw_klen);
				kfree(swd->sw_octx, M_CRYPTO_DATA);
			}
			break;

		case CRYPTO_AES_128_GMAC:
		case CRYPTO_AES_192_GMAC:
		case CRYPTO_AES_256_GMAC:
		case CRYPTO_MD5:
		case CRYPTO_SHA1:
			axf = swd->sw_axf;

			if (swd->sw_ictx) {
				bzero(swd->sw_ictx, axf->ctxsize);
				kfree(swd->sw_ictx, M_CRYPTO_DATA);
			}
			break;

		case CRYPTO_DEFLATE_COMP:
			break;
		}

		//FREE(swd, M_CRYPTO_DATA);
		kfree(swd, M_CRYPTO_DATA);
	}
	return 0;
}

/*
 * Process a software request.
 */
static int
swcr_process(device_t dev, struct cryptop *crp, int hint)
{
	struct cryptodesc *crd;
	struct swcr_data *sw;
	u_int32_t lid;

	/* Sanity check */
	if (crp == NULL)
		return EINVAL;

	if (crp->crp_desc == NULL || crp->crp_buf == NULL) {
		crp->crp_etype = EINVAL;
		goto done;
	}

	lid = crp->crp_sid & 0xffffffff;
	if (lid >= swcr_sesnum || lid == 0 || swcr_sessions[lid] == NULL) {
		crp->crp_etype = ENOENT;
		goto done;
	}

	/* Go through crypto descriptors, processing as we go */
	for (crd = crp->crp_desc; crd; crd = crd->crd_next) {
		/*
		 * Find the crypto context.
		 *
		 * XXX Note that the logic here prevents us from having
		 * XXX the same algorithm multiple times in a session
		 * XXX (or rather, we can but it won't give us the right
		 * XXX results). To do that, we'd need some way of differentiating
		 * XXX between the various instances of an algorithm (so we can
		 * XXX locate the correct crypto context).
		 */
		for (sw = swcr_sessions[lid];
		    sw && sw->sw_alg != crd->crd_alg;
		    sw = sw->sw_next)
			;

		/* No such context ? */
		if (sw == NULL) {
			crp->crp_etype = EINVAL;
			goto done;
		}
		switch (sw->sw_alg) {
		case CRYPTO_DES_CBC:
		case CRYPTO_3DES_CBC:
		case CRYPTO_BLF_CBC:
		case CRYPTO_CAST_CBC:
		case CRYPTO_SKIPJACK_CBC:
		case CRYPTO_RIJNDAEL128_CBC:
		case CRYPTO_AES_XTS:
		case CRYPTO_AES_CTR:
		case CRYPTO_CAMELLIA_CBC:
		case CRYPTO_TWOFISH_CBC:
		case CRYPTO_SERPENT_CBC:
		case CRYPTO_TWOFISH_XTS:
		case CRYPTO_SERPENT_XTS:
			if ((crp->crp_etype = swcr_encdec(crd, sw,
			    crp->crp_buf, crp->crp_flags)) != 0)
				goto done;
			break;
		case CRYPTO_NULL_CBC:
			crp->crp_etype = 0;
			break;
		case CRYPTO_MD5_HMAC:
		case CRYPTO_SHA1_HMAC:
		case CRYPTO_SHA2_256_HMAC:
		case CRYPTO_SHA2_384_HMAC:
		case CRYPTO_SHA2_512_HMAC:
		case CRYPTO_RIPEMD160_HMAC:
		case CRYPTO_NULL_HMAC:
		case CRYPTO_MD5_KPDK:
		case CRYPTO_SHA1_KPDK:
		case CRYPTO_MD5:
		case CRYPTO_SHA1:
			if ((crp->crp_etype = swcr_authcompute(crd, sw,
			    crp->crp_buf, crp->crp_flags)) != 0)
				goto done;
			break;

		case CRYPTO_AES_GCM_16:
		case CRYPTO_AES_GMAC:
		case CRYPTO_AES_128_GMAC:
		case CRYPTO_AES_192_GMAC:
		case CRYPTO_AES_256_GMAC:
			crp->crp_etype = swcr_combined(crp);
			goto done;

		case CRYPTO_DEFLATE_COMP:
			if ((crp->crp_etype = swcr_compdec(crd, sw, 
			    crp->crp_buf, crp->crp_flags)) != 0)
				goto done;
			else
				crp->crp_olen = (int)sw->sw_size;
			break;

		default:
			/* Unknown/unsupported algorithm */
			crp->crp_etype = EINVAL;
			goto done;
		}
	}

done:
	crypto_done(crp);
	lwkt_yield();
	return 0;
}

static void
swcr_identify(driver_t *drv, device_t parent)
{
	/* NB: order 10 is so we get attached after h/w devices */
	/* XXX: wouldn't bet about this BUS_ADD_CHILD correctness */
	if (device_find_child(parent, "cryptosoft", -1) == NULL &&
	    BUS_ADD_CHILD(parent, parent, 10, "cryptosoft", -1) == 0)
		panic("cryptosoft: could not attach");
}

static int
swcr_probe(device_t dev)
{
	device_set_desc(dev, "software crypto");
	return (0);
}

static int
swcr_attach(device_t dev)
{
	memset(hmac_ipad_buffer, HMAC_IPAD_VAL, HMAC_MAX_BLOCK_LEN);
	memset(hmac_opad_buffer, HMAC_OPAD_VAL, HMAC_MAX_BLOCK_LEN);

	swcr_id = crypto_get_driverid(dev, CRYPTOCAP_F_SOFTWARE |
					   CRYPTOCAP_F_SYNC |
					   CRYPTOCAP_F_SMP);
	if (swcr_id < 0) {
		device_printf(dev, "cannot initialize!");
		return ENOMEM;
	}
#define	REGISTER(alg) \
	crypto_register(swcr_id, alg, 0,0)
	REGISTER(CRYPTO_DES_CBC);
	REGISTER(CRYPTO_3DES_CBC);
	REGISTER(CRYPTO_BLF_CBC);
	REGISTER(CRYPTO_CAST_CBC);
	REGISTER(CRYPTO_SKIPJACK_CBC);
	REGISTER(CRYPTO_NULL_CBC);
	REGISTER(CRYPTO_MD5_HMAC);
	REGISTER(CRYPTO_SHA1_HMAC);
	REGISTER(CRYPTO_SHA2_256_HMAC);
	REGISTER(CRYPTO_SHA2_384_HMAC);
	REGISTER(CRYPTO_SHA2_512_HMAC);
	REGISTER(CRYPTO_RIPEMD160_HMAC);
	REGISTER(CRYPTO_NULL_HMAC);
	REGISTER(CRYPTO_MD5_KPDK);
	REGISTER(CRYPTO_SHA1_KPDK);
	REGISTER(CRYPTO_MD5);
	REGISTER(CRYPTO_SHA1);
	REGISTER(CRYPTO_RIJNDAEL128_CBC);
	REGISTER(CRYPTO_AES_XTS);
	REGISTER(CRYPTO_AES_CTR);
	REGISTER(CRYPTO_AES_GCM_16);
	REGISTER(CRYPTO_AES_GMAC);
	REGISTER(CRYPTO_AES_128_GMAC);
	REGISTER(CRYPTO_AES_192_GMAC);
	REGISTER(CRYPTO_AES_256_GMAC);
	REGISTER(CRYPTO_CAMELLIA_CBC);
	REGISTER(CRYPTO_TWOFISH_CBC);
	REGISTER(CRYPTO_SERPENT_CBC);
	REGISTER(CRYPTO_TWOFISH_XTS);
	REGISTER(CRYPTO_SERPENT_XTS);
	REGISTER(CRYPTO_DEFLATE_COMP);
#undef REGISTER

	return 0;
}

static int
swcr_detach(device_t dev)
{
	crypto_unregister_all(swcr_id);
	if (swcr_sessions != NULL)
		kfree(swcr_sessions, M_CRYPTO_DATA);
	return 0;
}

static device_method_t swcr_methods[] = {
	DEVMETHOD(device_identify,	swcr_identify),
	DEVMETHOD(device_probe,		swcr_probe),
	DEVMETHOD(device_attach,	swcr_attach),
	DEVMETHOD(device_detach,	swcr_detach),

	DEVMETHOD(cryptodev_newsession,	swcr_newsession),
	DEVMETHOD(cryptodev_freesession,swcr_freesession),
	DEVMETHOD(cryptodev_process,	swcr_process),

	DEVMETHOD_END
};

static driver_t swcr_driver = {
	"cryptosoft",
	swcr_methods,
	0,		/* NB: no softc */
};
static devclass_t swcr_devclass;

/*
 * NB: We explicitly reference the crypto module so we
 * get the necessary ordering when built as a loadable
 * module.  This is required because we bundle the crypto
 * module code together with the cryptosoft driver (otherwise
 * normal module dependencies would handle things).
 */
extern int crypto_modevent(struct module *, int, void *);
/* XXX where to attach */
DRIVER_MODULE(cryptosoft, nexus, swcr_driver, swcr_devclass, crypto_modevent,NULL);
MODULE_VERSION(cryptosoft, 1);
MODULE_DEPEND(cryptosoft, crypto, 1, 1, 1);
