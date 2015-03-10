/*
 * Copyright (c) 2011-2012 The DragonFly Project.  All rights reserved.
 *
 * This code is derived from software contributed to The DragonFly Project
 * by Matthew Dillon <dillon@dragonflybsd.org>
 * by Venkatesh Srinivas <vsrinivas@dragonflybsd.org>
 * by Alex Hornung <alexh@dragonflybsd.org>
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

#include "dmsg_local.h"

/*
 * Setup crypto for pthreads
 */
static pthread_mutex_t *crypto_locks;
int crypto_count;

static int dmsg_crypto_gcm_init(dmsg_ioq_t *, char *, int, char *, int, int);
static int dmsg_crypto_gcm_encrypt_chunk(dmsg_ioq_t *, char *, char *, int, int *);
static int dmsg_crypto_gcm_decrypt_chunk(dmsg_ioq_t *, char *, char *, int, int *);

/*
 * NOTE: the order of this table needs to match the DMSG_CRYPTO_ALGO_*_IDX
 *       defines in network.h.
 */
static struct crypto_algo crypto_algos[] = {
	{
		.name      = "aes-256-gcm",
		.keylen    = DMSG_CRYPTO_GCM_KEY_SIZE,
		.taglen    = DMSG_CRYPTO_GCM_TAG_SIZE,
		.init      = dmsg_crypto_gcm_init,
		.enc_chunk = dmsg_crypto_gcm_encrypt_chunk,
		.dec_chunk = dmsg_crypto_gcm_decrypt_chunk
	},
	{ NULL, 0, 0, NULL, NULL, NULL }
};

static
unsigned long
dmsg_crypto_id_callback(void)
{
	return ((unsigned long)(uintptr_t)pthread_self());
}

static
void
dmsg_crypto_locking_callback(int mode, int type,
				const char *file __unused, int line __unused)
{
	assert(type >= 0 && type < crypto_count);
	if (mode & CRYPTO_LOCK) {
		pthread_mutex_lock(&crypto_locks[type]);
	} else {
		pthread_mutex_unlock(&crypto_locks[type]);
	}
}

void
dmsg_crypto_setup(void)
{
	crypto_count = CRYPTO_num_locks();
	crypto_locks = calloc(crypto_count, sizeof(crypto_locks[0]));
	CRYPTO_set_id_callback(dmsg_crypto_id_callback);
	CRYPTO_set_locking_callback(dmsg_crypto_locking_callback);
}

static
int
dmsg_crypto_gcm_init(dmsg_ioq_t *ioq, char *key, int klen,
			char *iv_fixed, int ivlen, int enc)
{
	int i, ok;

	if (klen < DMSG_CRYPTO_GCM_KEY_SIZE ||
	    ivlen < DMSG_CRYPTO_GCM_IV_FIXED_SIZE) {
		dm_printf(1, "%s\n", "Not enough key or iv material");
		return -1;
	}

	dm_printf(6, "%s key: ", enc ? "Encryption" : "Decryption");
	for (i = 0; i < DMSG_CRYPTO_GCM_KEY_SIZE; ++i)
		dmx_printf(6, "%02x", (unsigned char)key[i]);
	dmx_printf(6, "%s\n", "");

	dm_printf(6, "%s iv:  ", enc ? "Encryption" : "Decryption");
	for (i = 0; i < DMSG_CRYPTO_GCM_IV_FIXED_SIZE; ++i)
		dmx_printf(6, "%02x", (unsigned char)iv_fixed[i]);
	dmx_printf(6, "%s\n", " (fixed part only)");

	EVP_CIPHER_CTX_init(&ioq->ctx);

	if (enc)
		ok = EVP_EncryptInit_ex(&ioq->ctx, EVP_aes_256_gcm(), NULL,
					key, NULL);
	else
		ok = EVP_DecryptInit_ex(&ioq->ctx, EVP_aes_256_gcm(), NULL,
					key, NULL);
	if (!ok)
		goto fail;

	/*
	 * According to the original Galois/Counter Mode of Operation (GCM)
	 * proposal, only IVs that are exactly 96 bits get used without any
	 * further processing. Other IV sizes cause the GHASH() operation
	 * to be applied to the IV, which is more costly.
	 *
	 * The NIST SP 800-38D also recommends using a 96 bit IV for the same
	 * reasons. We actually follow the deterministic construction
	 * recommended in NIST SP 800-38D with a 64 bit invocation field as an
	 * integer counter and a random, session-specific fixed field.
	 *
	 * This means that we can essentially use the same session key and
	 * IV fixed field for up to 2^64 invocations of the authenticated
	 * encryption or decryption.
	 *
	 * With a chunk size of 64 bytes, this adds up to 1 zettabyte of
	 * traffic.
	 */
	ok = EVP_CIPHER_CTX_ctrl(&ioq->ctx, EVP_CTRL_GCM_SET_IVLEN,
				 DMSG_CRYPTO_GCM_IV_SIZE, NULL);
	if (!ok)
		goto fail;

	memset(ioq->iv, 0, DMSG_CRYPTO_GCM_IV_SIZE);
	memcpy(ioq->iv, iv_fixed, DMSG_CRYPTO_GCM_IV_FIXED_SIZE);

	/*
	 * Strictly speaking, padding is irrelevant with a counter mode
	 * encryption.
	 *
	 * However, setting padding to 0, even if using a counter mode such
	 * as GCM, will cause an error in _finish if the pt/ct size is not
	 * a multiple of the cipher block size.
	 */
	EVP_CIPHER_CTX_set_padding(&ioq->ctx, 0);

	return 0;

fail:
	dm_printf(1, "%s\n", "Error during _gcm_init");
	return -1;
}

static
int
_gcm_iv_increment(char *iv)
{
	/*
	 * Deterministic construction according to NIST SP 800-38D, with
	 * 64 bit invocation field as integer counter.
	 *
	 * In other words, our 96 bit IV consists of a 32 bit fixed field
	 * unique to the session and a 64 bit integer counter.
	 */

	uint64_t *c = (uint64_t *)(&iv[DMSG_CRYPTO_GCM_IV_FIXED_SIZE]);

	/* Increment invocation field integer counter */
	*c = htobe64(be64toh(*c)+1);

	/*
	 * Detect wrap-around, which means it is time to renegotiate
	 * the session to get a new key and/or fixed field.
	 */
	return (*c == 0) ? 0 : 1;
}

static
int
dmsg_crypto_gcm_encrypt_chunk(dmsg_ioq_t *ioq, char *ct, char *pt,
				 int in_size, int *out_size)
{
	int ok;
	int u_len, f_len;

	*out_size = 0;

	/* Re-initialize with new IV (but without redoing the key schedule) */
	ok = EVP_EncryptInit_ex(&ioq->ctx, NULL, NULL, NULL, ioq->iv);
	if (!ok)
		goto fail;

	u_len = 0;	/* safety */
	ok = EVP_EncryptUpdate(&ioq->ctx, ct, &u_len, pt, in_size);
	if (!ok)
		goto fail;

	f_len = 0;	/* safety */
	ok = EVP_EncryptFinal(&ioq->ctx, ct + u_len, &f_len);
	if (!ok)
		goto fail;

	/* Retrieve auth tag */
	ok = EVP_CIPHER_CTX_ctrl(&ioq->ctx, EVP_CTRL_GCM_GET_TAG,
				 DMSG_CRYPTO_GCM_TAG_SIZE,
				 ct + u_len + f_len);
	if (!ok)
		goto fail;

	ok = _gcm_iv_increment(ioq->iv);
	if (!ok) {
		ioq->error = DMSG_IOQ_ERROR_IVWRAP;
		goto fail_out;
	}

	*out_size = u_len + f_len + DMSG_CRYPTO_GCM_TAG_SIZE;

	return 0;

fail:
	ioq->error = DMSG_IOQ_ERROR_ALGO;
fail_out:
	dm_printf(1, "%s\n", "error during encrypt_chunk");
	return -1;
}

static
int
dmsg_crypto_gcm_decrypt_chunk(dmsg_ioq_t *ioq, char *ct, char *pt,
				 int out_size, int *consume_size)
{
	int ok;
	int u_len, f_len;

	*consume_size = 0;

	/* Re-initialize with new IV (but without redoing the key schedule) */
	ok = EVP_DecryptInit_ex(&ioq->ctx, NULL, NULL, NULL, ioq->iv);
	if (!ok) {
		ioq->error = DMSG_IOQ_ERROR_ALGO;
		goto fail_out;
	}

	ok = EVP_CIPHER_CTX_ctrl(&ioq->ctx, EVP_CTRL_GCM_SET_TAG,
				 DMSG_CRYPTO_GCM_TAG_SIZE,
				 ct + out_size);
	if (!ok) {
		ioq->error = DMSG_IOQ_ERROR_ALGO;
		goto fail_out;
	}

	ok = EVP_DecryptUpdate(&ioq->ctx, pt, &u_len, ct, out_size);
	if (!ok)
		goto fail;

	ok = EVP_DecryptFinal(&ioq->ctx, pt + u_len, &f_len);
	if (!ok)
		goto fail;

	ok = _gcm_iv_increment(ioq->iv);
	if (!ok) {
		ioq->error = DMSG_IOQ_ERROR_IVWRAP;
		goto fail_out;
	}

	*consume_size = u_len + f_len + DMSG_CRYPTO_GCM_TAG_SIZE;

	return 0;

fail:
	ioq->error = DMSG_IOQ_ERROR_MACFAIL;
fail_out:
	dm_printf(1, "%s\n",
		  "error during decrypt_chunk "
		  "(likely authentication error)");
	return -1;
}

/*
 * Synchronously negotiate crypto for a new session.  This must occur
 * within 10 seconds or the connection is error'd out.
 *
 * We work off the IP address and/or reverse DNS.  The IP address is
 * checked first, followed by the IP address at various levels of granularity,
 * followed by the full domain name and domain names at various levels of
 * granularity.
 *
 *	/etc/hammer2/remote/<name>.pub	- Contains a public key
 *	/etc/hammer2/remote/<name>.none	- Indicates no encryption (empty file)
 *					  (e.g. localhost.none).
 *
 * We first attempt to locate a public key file based on the peer address or
 * peer FQDN.
 *
 *	<name>.none	- No further negotiation is needed.  We simply return.
 *			  All communication proceeds without encryption.
 *			  No public key handshake occurs in this situation.
 *			  (both ends must match).
 *
 *	<name>.pub	- We have located the public key for the peer.  Both
 *			  sides transmit a block encrypted with their private
 *			  keys and the peer's public key.
 *
 *			  Both sides receive a block and decrypt it.
 *
 *			  Both sides formulate a reply using the decrypted
 *			  block and transmit it.
 *
 *			  communication proceeds with the negotiated session
 *			  key (typically AES-256-CBC).
 *
 * If we fail to locate the appropriate file and no floating.db exists the
 * connection is terminated without further action.
 *
 * If floating.db exists the connection proceeds with a floating negotiation.
 */
typedef union {
	struct sockaddr sa;
	struct sockaddr_in sa_in;
	struct sockaddr_in6 sa_in6;
} sockaddr_any_t;

void
dmsg_crypto_negotiate(dmsg_iocom_t *iocom)
{
	sockaddr_any_t sa;
	socklen_t salen = sizeof(sa);
	char peername[128];
	char realname[128];
	dmsg_handshake_t handtx;
	dmsg_handshake_t handrx;
	char buf1[sizeof(handtx)];
	char buf2[sizeof(handtx)];
	char *ptr;
	char *path;
	struct stat st;
	FILE *fp;
	RSA *keys[3] = { NULL, NULL, NULL };
	size_t i;
	size_t blksize;
	size_t blkmask;
	ssize_t n;
	int fd;
	int error;

	/*
	 * Get the peer IP address for the connection as a string.
	 */
	if (getpeername(iocom->sock_fd, &sa.sa, &salen) < 0) {
		iocom->ioq_rx.error = DMSG_IOQ_ERROR_NOPEER;
		atomic_set_int(&iocom->flags, DMSG_IOCOMF_EOF);
		dm_printf(1, "%s\n", "accept: getpeername() failed");
		goto done;
	}
	if (getnameinfo(&sa.sa, salen, peername, sizeof(peername),
			NULL, 0, NI_NUMERICHOST) < 0) {
		iocom->ioq_rx.error = DMSG_IOQ_ERROR_NOPEER;
		atomic_set_int(&iocom->flags, DMSG_IOCOMF_EOF);
		dm_printf(1, "%s\n", "accept: cannot decode sockaddr");
		goto done;
	}
	if (DMsgDebugOpt) {
		if (realhostname_sa(realname, sizeof(realname),
				    &sa.sa, salen) == HOSTNAME_FOUND) {
			dm_printf(1, "accept from %s (%s)\n",
				  peername, realname);
		} else {
			dm_printf(1, "accept from %s\n", peername);
		}
	}

	/*
	 * Find the remote host's public key
	 *
	 * If the link is not to be encrypted (<ip>.none located) we shortcut
	 * the handshake entirely.  No buffers are exchanged.
	 */
	asprintf(&path, "%s/%s.pub", DMSG_PATH_REMOTE, peername);
	if ((fp = fopen(path, "r")) == NULL) {
		free(path);
		asprintf(&path, "%s/%s.none",
			 DMSG_PATH_REMOTE, peername);
		if (stat(path, &st) < 0) {
			iocom->ioq_rx.error = DMSG_IOQ_ERROR_NORKEY;
			atomic_set_int(&iocom->flags, DMSG_IOCOMF_EOF);
			dm_printf(1, "%s\n", "auth failure: unknown host");
			goto done;
		}
		dm_printf(1, "%s\n", "auth succeeded, unencrypted link");
		goto done;
	}
	if (fp) {
		keys[0] = PEM_read_RSA_PUBKEY(fp, NULL, NULL, NULL);
		fclose(fp);
		if (keys[0] == NULL) {
			iocom->ioq_rx.error = DMSG_IOQ_ERROR_KEYFMT;
			atomic_set_int(&iocom->flags, DMSG_IOCOMF_EOF);
			dm_printf(1, "%s\n", "auth failure: bad key format");
			goto done;
		}
	}

	/*
	 * Get our public and private keys
	 */
	free(path);
	asprintf(&path, DMSG_DEFAULT_DIR "/rsa.pub");
	if ((fp = fopen(path, "r")) == NULL) {
		iocom->ioq_rx.error = DMSG_IOQ_ERROR_NOLKEY;
		atomic_set_int(&iocom->flags, DMSG_IOCOMF_EOF);
		goto done;
	}
	keys[1] = PEM_read_RSA_PUBKEY(fp, NULL, NULL, NULL);
	fclose(fp);
	if (keys[1] == NULL) {
		iocom->ioq_rx.error = DMSG_IOQ_ERROR_KEYFMT;
		atomic_set_int(&iocom->flags, DMSG_IOCOMF_EOF);
		dm_printf(1, "%s\n", "auth failure: bad host key format");
		goto done;
	}

	free(path);
	asprintf(&path, DMSG_DEFAULT_DIR "/rsa.prv");
	if ((fp = fopen(path, "r")) == NULL) {
		iocom->ioq_rx.error = DMSG_IOQ_ERROR_NOLKEY;
		atomic_set_int(&iocom->flags, DMSG_IOCOMF_EOF);
		dm_printf(1, "%s\n", "auth failure: bad host key format");
		goto done;
	}
	keys[2] = PEM_read_RSAPrivateKey(fp, NULL, NULL, NULL);
	fclose(fp);
	if (keys[2] == NULL) {
		iocom->ioq_rx.error = DMSG_IOQ_ERROR_KEYFMT;
		atomic_set_int(&iocom->flags, DMSG_IOCOMF_EOF);
		dm_printf(1, "%s\n", "auth failure: bad host key format");
		goto done;
	}
	free(path);
	path = NULL;

	/*
	 * public key encrypt/decrypt block size.
	 */
	if (keys[0]) {
		blksize = (size_t)RSA_size(keys[0]);
		if (blksize != (size_t)RSA_size(keys[1]) ||
		    blksize != (size_t)RSA_size(keys[2]) ||
		    sizeof(handtx) % blksize != 0) {
			iocom->ioq_rx.error = DMSG_IOQ_ERROR_KEYFMT;
			atomic_set_int(&iocom->flags, DMSG_IOCOMF_EOF);
			dm_printf(1, "%s\n",
				  "auth failure: key size mismatch");
			goto done;
		}
	} else {
		blksize = sizeof(handtx);
	}
	blkmask = blksize - 1;

	bzero(&handrx, sizeof(handrx));
	bzero(&handtx, sizeof(handtx));

	/*
	 * Fill all unused fields (particular all junk fields) with random
	 * data, and also set the session key.
	 */
	fd = open("/dev/urandom", O_RDONLY);
	if (fd < 0 ||
	    fstat(fd, &st) < 0 ||	/* something wrong */
	    S_ISREG(st.st_mode) ||	/* supposed to be a RNG dev! */
	    read(fd, &handtx, sizeof(handtx)) != sizeof(handtx)) {
urandfail:
		if (fd >= 0)
			close(fd);
		iocom->ioq_rx.error = DMSG_IOQ_ERROR_BADURANDOM;
		atomic_set_int(&iocom->flags, DMSG_IOCOMF_EOF);
		dm_printf(1, "%s\n", "auth failure: bad rng");
		goto done;
	}
	if (bcmp(&handrx, &handtx, sizeof(handtx)) == 0)
		goto urandfail;			/* read all zeros */
	close(fd);
	/* ERR_load_crypto_strings(); openssl debugging */

	/*
	 * Handshake with the remote.
	 *
	 *	Encrypt with my private and remote's public
	 *	Decrypt with my private and remote's public
	 *
	 * When encrypting we have to make sure our buffer fits within the
	 * modulus, which typically requires bit 7 o the first byte to be
	 * zero.  To be safe make sure that bit 7 and bit 6 is zero.
	 */
	snprintf(handtx.quickmsg, sizeof(handtx.quickmsg), "Testing 1 2 3");
	handtx.magic = DMSG_HDR_MAGIC;
	handtx.version = 1;
	handtx.flags = 0;
	assert(sizeof(handtx.verf) * 4 == sizeof(handtx.sess));
	bzero(handtx.verf, sizeof(handtx.verf));

	handtx.pad1[0] &= 0x3f;	/* message must fit within modulus */
	handtx.pad2[0] &= 0x3f;	/* message must fit within modulus */

	for (i = 0; i < sizeof(handtx.sess); ++i)
		handtx.verf[i / 4] ^= handtx.sess[i];

	/*
	 * Write handshake buffer to remote
	 */
	for (i = 0; i < sizeof(handtx); i += blksize) {
		ptr = (char *)&handtx + i;
		if (keys[0]) {
			/*
			 * Since we are double-encrypting we have to make
			 * sure that the result of the first stage does
			 * not blow out the modulus for the second stage.
			 *
			 * The pointer is pointing to the pad*[] area so
			 * we can mess with that until the first stage
			 * is legal.
			 */
			do {
				++*(int *)(ptr + 4);
				if (RSA_private_encrypt(blksize, ptr, buf1,
					    keys[2], RSA_NO_PADDING) < 0) {
					iocom->ioq_rx.error =
						DMSG_IOQ_ERROR_KEYXCHGFAIL;
				}
			} while (buf1[0] & 0xC0);

			if (RSA_public_encrypt(blksize, buf1, buf2,
					    keys[0], RSA_NO_PADDING) < 0) {
				iocom->ioq_rx.error =
					DMSG_IOQ_ERROR_KEYXCHGFAIL;
			}
		}
		if (write(iocom->sock_fd, buf2, blksize) != (ssize_t)blksize) {
			dmio_printf(iocom, 1, "%s\n", "WRITE ERROR");
		}
	}
	if (iocom->ioq_rx.error) {
		atomic_set_int(&iocom->flags, DMSG_IOCOMF_EOF);
		dmio_printf(iocom, 1, "%s\n",
			    "auth failure: key exchange failure "
			    "during encryption");
		goto done;
	}

	/*
	 * Read handshake buffer from remote
	 */
	i = 0;
	while (i < sizeof(handrx)) {
		ptr = (char *)&handrx + i;
		n = read(iocom->sock_fd, ptr, blksize - (i & blkmask));
		if (n <= 0)
			break;
		ptr -= (i & blkmask);
		i += n;
		if (keys[0] && (i & blkmask) == 0) {
			if (RSA_private_decrypt(blksize, ptr, buf1,
					   keys[2], RSA_NO_PADDING) < 0)
				iocom->ioq_rx.error =
						DMSG_IOQ_ERROR_KEYXCHGFAIL;
			if (RSA_public_decrypt(blksize, buf1, ptr,
					   keys[0], RSA_NO_PADDING) < 0)
				iocom->ioq_rx.error =
						DMSG_IOQ_ERROR_KEYXCHGFAIL;
		}
	}
	if (iocom->ioq_rx.error) {
		atomic_set_int(&iocom->flags, DMSG_IOCOMF_EOF);
		dmio_printf(iocom, 1, "%s\n",
			    "auth failure: key exchange failure "
			    "during decryption");
		goto done;
	}

	/*
	 * Validate the received data.  Try to make this a constant-time
	 * algorithm.
	 */
	if (i != sizeof(handrx)) {
keyxchgfail:
		iocom->ioq_rx.error = DMSG_IOQ_ERROR_KEYXCHGFAIL;
		atomic_set_int(&iocom->flags, DMSG_IOCOMF_EOF);
		dmio_printf(iocom, 1, "%s\n",
			    "auth failure: key exchange failure");
		goto done;
	}

	if (handrx.magic == DMSG_HDR_MAGIC_REV) {
		handrx.version = bswap16(handrx.version);
		handrx.flags = bswap32(handrx.flags);
	}
	for (i = 0; i < sizeof(handrx.sess); ++i)
		handrx.verf[i / 4] ^= handrx.sess[i];
	n = 0;
	for (i = 0; i < sizeof(handrx.verf); ++i)
		n += handrx.verf[i];
	if (handrx.version != 1)
		++n;
	if (n != 0)
		goto keyxchgfail;

	/*
	 * Use separate session keys and session fixed IVs for receive and
	 * transmit.
	 */
	error = crypto_algos[DMSG_CRYPTO_ALGO].init(&iocom->ioq_rx, handrx.sess,
	    crypto_algos[DMSG_CRYPTO_ALGO].keylen,
	    handrx.sess + crypto_algos[DMSG_CRYPTO_ALGO].keylen,
	    sizeof(handrx.sess) - crypto_algos[DMSG_CRYPTO_ALGO].keylen,
	    0 /* decryption */);
	if (error)
		goto keyxchgfail;

	error = crypto_algos[DMSG_CRYPTO_ALGO].init(&iocom->ioq_tx, handtx.sess,
	    crypto_algos[DMSG_CRYPTO_ALGO].keylen,
	    handtx.sess + crypto_algos[DMSG_CRYPTO_ALGO].keylen,
	    sizeof(handtx.sess) - crypto_algos[DMSG_CRYPTO_ALGO].keylen,
	    1 /* encryption */);
	if (error)
		goto keyxchgfail;

	atomic_set_int(&iocom->flags, DMSG_IOCOMF_CRYPTED);

	dmio_printf(iocom, 1, "auth success: %s\n", handrx.quickmsg);
done:
	if (path)
		free(path);
	if (keys[0])
		RSA_free(keys[0]);
	if (keys[1])
		RSA_free(keys[1]);
	if (keys[1])
		RSA_free(keys[2]);
}

/*
 * Decrypt pending data in the ioq's fifo.  The data is decrypted in-place.
 */
void
dmsg_crypto_decrypt(dmsg_iocom_t *iocom __unused, dmsg_ioq_t *ioq)
{
	int p_len;
	int used;
	__unused int error;	/* XXX */
	char buf[512];

	/*
	 * fifo_beg to fifo_cdx is data already decrypted.
	 * fifo_cdn to fifo_end is data not yet decrypted.
	 */
	p_len = ioq->fifo_end - ioq->fifo_cdn; /* data not yet decrypted */

	if (p_len == 0)
		return;

	while (p_len >= crypto_algos[DMSG_CRYPTO_ALGO].taglen +
	    DMSG_CRYPTO_CHUNK_SIZE) {
		bcopy(ioq->buf + ioq->fifo_cdn, buf,
		      crypto_algos[DMSG_CRYPTO_ALGO].taglen +
		      DMSG_CRYPTO_CHUNK_SIZE);
		error = crypto_algos[DMSG_CRYPTO_ALGO].dec_chunk(
		    ioq, buf,
		    ioq->buf + ioq->fifo_cdx,
		    DMSG_CRYPTO_CHUNK_SIZE,
		    &used);
#ifdef CRYPTO_DEBUG
		dmio_printf(iocom, 5,
			    "dec: p_len: %d, used: %d, "
			    "fifo_cdn: %ju, fifo_cdx: %ju\n",
			     p_len, used,
			     ioq->fifo_cdn, ioq->fifo_cdx);
#endif
		p_len -= used;
		ioq->fifo_cdn += used;
		ioq->fifo_cdx += DMSG_CRYPTO_CHUNK_SIZE;
#ifdef CRYPTO_DEBUG
		dmio_printf(iocom, 5,
			    "dec: p_len: %d, used: %d, "
			    "fifo_cdn: %ju, fifo_cdx: %ju\n",
			    p_len, used, ioq->fifo_cdn, ioq->fifo_cdx);
#endif
	}
}

/*
 * *nactp is set to the number of ORIGINAL bytes consumed by the encrypter.
 * The FIFO may contain more data.
 */
int
dmsg_crypto_encrypt(dmsg_iocom_t *iocom __unused, dmsg_ioq_t *ioq,
		    struct iovec *iov, int n, size_t *nactp)
{
	int p_len, used, ct_used;
	int i;
	__unused int error;	/* XXX */
	size_t nmax;

	nmax = sizeof(ioq->buf) - ioq->fifo_end;	/* max new bytes */

	*nactp = 0;
	for (i = 0; i < n && nmax; ++i) {
		used = 0;
		p_len = iov[i].iov_len;
		assert((p_len & DMSG_ALIGNMASK) == 0);

		while (p_len >= DMSG_CRYPTO_CHUNK_SIZE &&
		    nmax >= DMSG_CRYPTO_CHUNK_SIZE +
		    (size_t)crypto_algos[DMSG_CRYPTO_ALGO].taglen) {
			error = crypto_algos[DMSG_CRYPTO_ALGO].enc_chunk(
			    ioq,
			    ioq->buf + ioq->fifo_cdx,
			    (char *)iov[i].iov_base + used,
			    DMSG_CRYPTO_CHUNK_SIZE, &ct_used);
#ifdef CRYPTO_DEBUG
			dmio_printf(iocom, 5,
				    "nactp: %ju, p_len: %d, "
				    "ct_used: %d, used: %d, nmax: %ju\n",
				    *nactp, p_len, ct_used, used, nmax);
#endif

			*nactp += (size_t)DMSG_CRYPTO_CHUNK_SIZE;	/* plaintext count */
			used += DMSG_CRYPTO_CHUNK_SIZE;
			p_len -= DMSG_CRYPTO_CHUNK_SIZE;

			/*
			 * NOTE: crypted count will eventually differ from
			 *	 nmax, but for now we have not yet introduced
			 *	 random armor.
			 */
			ioq->fifo_cdx += (size_t)ct_used;
			ioq->fifo_cdn += (size_t)ct_used;
			ioq->fifo_end += (size_t)ct_used;
			nmax -= (size_t)ct_used;
#ifdef CRYPTO_DEBUG
			dmio_printf(iocom, 5,
				    "nactp: %ju, p_len: %d, "
				    "ct_used: %d, used: %d, nmax: %ju\n",
				    *nactp, p_len, ct_used, used, nmax);
#endif
		}
	}
	iov[0].iov_base = ioq->buf + ioq->fifo_beg;
	iov[0].iov_len = ioq->fifo_cdx - ioq->fifo_beg;

	return (1);
}
