/*
 * Copyright (c) 2011-2012 The DragonFly Project.  All rights reserved.
 *
 * This code is derived from software contributed to The DragonFly Project
 * by Matthew Dillon <dillon@dragonflybsd.org>
 * by Venkatesh Srinivas <vsrinivas@dragonflybsd.org>
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

#include "hammer2.h"

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
hammer2_crypto_negotiate(hammer2_iocom_t *iocom)
{
	sockaddr_any_t sa;
	socklen_t salen = sizeof(sa);
	char peername[128];
	char realname[128];
	hammer2_handshake_t handtx;
	hammer2_handshake_t handrx;
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

	/*
	 * Get the peer IP address for the connection as a string.
	 */
	if (getpeername(iocom->sock_fd, &sa.sa, &salen) < 0) {
		iocom->ioq_rx.error = HAMMER2_IOQ_ERROR_NOPEER;
		iocom->flags |= HAMMER2_IOCOMF_EOF;
		if (DebugOpt)
			fprintf(stderr, "accept: getpeername() failed\n");
		goto done;
	}
	if (getnameinfo(&sa.sa, salen, peername, sizeof(peername),
			NULL, 0, NI_NUMERICHOST) < 0) {
		iocom->ioq_rx.error = HAMMER2_IOQ_ERROR_NOPEER;
		iocom->flags |= HAMMER2_IOCOMF_EOF;
		if (DebugOpt)
			fprintf(stderr, "accept: cannot decode sockaddr\n");
		goto done;
	}
	if (DebugOpt) {
		if (realhostname_sa(realname, sizeof(realname),
				    &sa.sa, salen) == HOSTNAME_FOUND) {
			fprintf(stderr, "accept from %s (%s)\n",
				peername, realname);
		} else {
			fprintf(stderr, "accept from %s\n", peername);
		}
	}

	/*
	 * Find the remote host's public key
	 *
	 * If the link is not to be encrypted (<ip>.none located) we shortcut
	 * the handshake entirely.  No buffers are exchanged.
	 */
	asprintf(&path, "%s/%s.pub", HAMMER2_PATH_REMOTE, peername);
	if ((fp = fopen(path, "r")) == NULL) {
		free(path);
		asprintf(&path, "%s/%s.none",
			 HAMMER2_PATH_REMOTE, peername);
		if (stat(path, &st) < 0) {
			iocom->ioq_rx.error = HAMMER2_IOQ_ERROR_NORKEY;
			iocom->flags |= HAMMER2_IOCOMF_EOF;
			if (DebugOpt)
				fprintf(stderr, "auth failure: unknown host\n");
			goto done;
		}
		if (DebugOpt)
			fprintf(stderr, "auth succeeded, unencrypted link\n");
		goto done;
	}
	if (fp) {
		keys[0] = PEM_read_RSA_PUBKEY(fp, NULL, NULL, NULL);
		fclose(fp);
		if (keys[0] == NULL) {
			iocom->ioq_rx.error = HAMMER2_IOQ_ERROR_KEYFMT;
			iocom->flags |= HAMMER2_IOCOMF_EOF;
			if (DebugOpt)
				fprintf(stderr,
					"auth failure: bad key format\n");
			goto done;
		}
	}

	/*
	 * Get our public and private keys
	 */
	free(path);
	asprintf(&path, HAMMER2_DEFAULT_DIR "/rsa.pub");
	if ((fp = fopen(path, "r")) == NULL) {
		iocom->ioq_rx.error = HAMMER2_IOQ_ERROR_NOLKEY;
		iocom->flags |= HAMMER2_IOCOMF_EOF;
		goto done;
	}
	keys[1] = PEM_read_RSA_PUBKEY(fp, NULL, NULL, NULL);
	fclose(fp);
	if (keys[1] == NULL) {
		iocom->ioq_rx.error = HAMMER2_IOQ_ERROR_KEYFMT;
		iocom->flags |= HAMMER2_IOCOMF_EOF;
		if (DebugOpt)
			fprintf(stderr, "auth failure: bad host key format\n");
		goto done;
	}

	free(path);
	asprintf(&path, HAMMER2_DEFAULT_DIR "/rsa.prv");
	if ((fp = fopen(path, "r")) == NULL) {
		iocom->ioq_rx.error = HAMMER2_IOQ_ERROR_NOLKEY;
		iocom->flags |= HAMMER2_IOCOMF_EOF;
		if (DebugOpt)
			fprintf(stderr, "auth failure: bad host key format\n");
		goto done;
	}
	keys[2] = PEM_read_RSAPrivateKey(fp, NULL, NULL, NULL);
	fclose(fp);
	if (keys[2] == NULL) {
		iocom->ioq_rx.error = HAMMER2_IOQ_ERROR_KEYFMT;
		iocom->flags |= HAMMER2_IOCOMF_EOF;
		if (DebugOpt)
			fprintf(stderr, "auth failure: bad host key format\n");
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
			iocom->ioq_rx.error = HAMMER2_IOQ_ERROR_KEYFMT;
			iocom->flags |= HAMMER2_IOCOMF_EOF;
			if (DebugOpt)
				fprintf(stderr, "auth failure: "
						"key size mismatch\n");
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
		iocom->ioq_rx.error = HAMMER2_IOQ_ERROR_BADURANDOM;
		iocom->flags |= HAMMER2_IOCOMF_EOF;
		if (DebugOpt)
			fprintf(stderr, "auth failure: bad rng\n");
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
	handtx.magic = HAMMER2_MSGHDR_MAGIC;
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
						HAMMER2_IOQ_ERROR_KEYXCHGFAIL;
				}
			} while (buf1[0] & 0xC0);

			if (RSA_public_encrypt(blksize, buf1, buf2,
					    keys[0], RSA_NO_PADDING) < 0) {
				iocom->ioq_rx.error =
					HAMMER2_IOQ_ERROR_KEYXCHGFAIL;
			}
		}
		if (write(iocom->sock_fd, buf2, blksize) != (ssize_t)blksize) {
			fprintf(stderr, "WRITE ERROR\n");
		}
	}
	if (iocom->ioq_rx.error) {
		iocom->flags |= HAMMER2_IOCOMF_EOF;
		if (DebugOpt)
			fprintf(stderr, "auth failure: key exchange failure "
					"during encryption\n");
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
						HAMMER2_IOQ_ERROR_KEYXCHGFAIL;
			if (RSA_public_decrypt(blksize, buf1, ptr,
					   keys[0], RSA_NO_PADDING) < 0)
				iocom->ioq_rx.error =
						HAMMER2_IOQ_ERROR_KEYXCHGFAIL;
		}
	}
	if (iocom->ioq_rx.error) {
		iocom->flags |= HAMMER2_IOCOMF_EOF;
		if (DebugOpt)
			fprintf(stderr, "auth failure: key exchange failure "
					"during decryption\n");
		goto done;
	}

	/*
	 * Validate the received data.  Try to make this a constant-time
	 * algorithm.
	 */
	if (i != sizeof(handrx)) {
keyxchgfail:
		iocom->ioq_rx.error = HAMMER2_IOQ_ERROR_KEYXCHGFAIL;
		iocom->flags |= HAMMER2_IOCOMF_EOF;
		if (DebugOpt)
			fprintf(stderr, "auth failure: key exchange failure\n");
		goto done;
	}

	if (handrx.magic == HAMMER2_MSGHDR_MAGIC_REV) {
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
	 * Calculate the session key and initialize the iv[].
	 */
	assert(HAMMER2_AES_KEY_SIZE * 2 == sizeof(handrx.sess));
	for (i = 0; i < HAMMER2_AES_KEY_SIZE; ++i) {
		iocom->sess[i] = handrx.sess[i] ^ handtx.sess[i];
		iocom->ioq_rx.iv[i] = handrx.sess[HAMMER2_AES_KEY_SIZE + i] ^
				      handtx.sess[HAMMER2_AES_KEY_SIZE + i];
		iocom->ioq_tx.iv[i] = handrx.sess[HAMMER2_AES_KEY_SIZE + i] ^
				      handtx.sess[HAMMER2_AES_KEY_SIZE + i];
	}
	printf("sess: ");
	for (i = 0; i < HAMMER2_AES_KEY_SIZE; ++i)
		printf("%02x", (unsigned char)iocom->sess[i]);
	printf("\n");
	printf("iv: ");
	for (i = 0; i < HAMMER2_AES_KEY_SIZE; ++i)
		printf("%02x", (unsigned char)iocom->ioq_rx.iv[i]);
	printf("\n");

	EVP_CIPHER_CTX_init(&iocom->ioq_rx.ctx);
	EVP_DecryptInit_ex(&iocom->ioq_rx.ctx, HAMMER2_AES_TYPE_EVP, NULL,
			   iocom->sess, iocom->ioq_rx.iv);
	EVP_CIPHER_CTX_set_padding(&iocom->ioq_rx.ctx, 0);

	EVP_CIPHER_CTX_init(&iocom->ioq_tx.ctx);
	EVP_EncryptInit_ex(&iocom->ioq_tx.ctx, HAMMER2_AES_TYPE_EVP, NULL,
			   iocom->sess, iocom->ioq_tx.iv);
	EVP_CIPHER_CTX_set_padding(&iocom->ioq_tx.ctx, 0);

	iocom->flags |= HAMMER2_IOCOMF_CRYPTED;

	if (DebugOpt)
		fprintf(stderr, "auth success: %s\n", handrx.quickmsg);
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
hammer2_crypto_decrypt(hammer2_iocom_t *iocom, hammer2_ioq_t *ioq)
{
	int p_len;
	int n;
	int i;
	char buf[512];

	if ((iocom->flags & HAMMER2_IOCOMF_CRYPTED) == 0)
		return;
	p_len = ioq->fifo_end - ioq->fifo_cdx;
	p_len &= ~HAMMER2_AES_KEY_MASK;
	if (p_len == 0)
		return;
	for (i = 0; i < p_len; i += n) {
		n = (p_len - i > (int)sizeof(buf)) ?
			(int)sizeof(buf) : p_len - i;
		bcopy(ioq->buf + ioq->fifo_cdx + i, buf, n);
		EVP_DecryptUpdate(&ioq->ctx,
				  ioq->buf + ioq->fifo_cdx + i, &n,
				  buf, n);
	}
	ioq->fifo_cdx += p_len;
}

/*
 * Decrypt data in the message's auxilary buffer.  The data is decrypted
 * in-place.
 */
void
hammer2_crypto_decrypt_aux(hammer2_iocom_t *iocom, hammer2_ioq_t *ioq,
			   hammer2_msg_t *msg, int already)
{
	int p_len;
	int n;
	int i;
	char buf[512];

	if ((iocom->flags & HAMMER2_IOCOMF_CRYPTED) == 0)
		return;
	p_len = msg->aux_size;
	assert((p_len & HAMMER2_AES_KEY_MASK) == 0);
	if (p_len == 0)
		return;
	i = already;
	while (i < p_len) {
		n = (p_len - i > (int)sizeof(buf)) ?
			(int)sizeof(buf) : p_len - i;
		bcopy(msg->aux_data + i, buf, n);
		EVP_DecryptUpdate(&ioq->ctx,
				  msg->aux_data + i, &n,
				  buf, n);
		i += n;
	}
#if 0
	EVP_DecryptUpdate(&iocom->ioq_rx.ctx,
			  msg->aux_data, &p_len,
			  msg->aux_data, p_len);
#endif
}

int
hammer2_crypto_encrypt(hammer2_iocom_t *iocom, hammer2_ioq_t *ioq,
		       struct iovec *iov, int n)
{
	int p_len;
	int i;
	int already;
	int nmax;

	if ((iocom->flags & HAMMER2_IOCOMF_CRYPTED) == 0)
		return (n);
	nmax = sizeof(ioq->buf) - ioq->fifo_cdx;	/* max new bytes */
	already = ioq->fifo_cdx - ioq->fifo_beg;	/* already encrypted */

	for (i = 0; i < n; ++i) {
		p_len = iov[i].iov_len;
		if (p_len <= already) {
			already -= p_len;
			continue;
		}
		p_len -= already;
		if (p_len > nmax)
			p_len = nmax;
		EVP_EncryptUpdate(&ioq->ctx,
				  ioq->buf + ioq->fifo_cdx, &p_len,
				  (char *)iov[i].iov_base + already, p_len);
		ioq->fifo_cdx += p_len;
		ioq->fifo_end += p_len;
		nmax -= p_len;
		if (nmax == 0)
			break;
		already = 0;
	}
	iov[0].iov_base = ioq->buf + ioq->fifo_beg;
	iov[0].iov_len = ioq->fifo_cdx - ioq->fifo_beg;

	return (1);
}

void
hammer2_crypto_encrypt_wrote(hammer2_iocom_t *iocom, hammer2_ioq_t *ioq,
			     int nact)
{
	if ((iocom->flags & HAMMER2_IOCOMF_CRYPTED) == 0)
		return;
	if (nact == 0)
		return;
	ioq->fifo_beg += nact;
	if (ioq->fifo_beg == ioq->fifo_end) {
		ioq->fifo_beg = 0;
		ioq->fifo_cdx = 0;
		ioq->fifo_end = 0;
	}
}
