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

#include <openssl/rsa.h>
#include <openssl/pem.h>
#include <openssl/err.h>

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
	char buf[sizeof(handtx)];
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
	ERR_load_crypto_strings();

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
				if (RSA_private_encrypt(blksize, ptr, buf,
					    keys[2], RSA_NO_PADDING) < 0) {
					iocom->ioq_rx.error =
						HAMMER2_IOQ_ERROR_KEYXCHGFAIL;
				}
			} while (buf[0] & 0xC0);

			if (RSA_public_encrypt(blksize, buf, ptr,
					    keys[0], RSA_NO_PADDING) < 0) {
				iocom->ioq_rx.error =
					HAMMER2_IOQ_ERROR_KEYXCHGFAIL;
			}
		}
		if (write(iocom->sock_fd, ptr, blksize) != (ssize_t)blksize) {
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
			if (RSA_private_decrypt(blksize, ptr, buf,
					   keys[2], RSA_NO_PADDING) < 0)
				iocom->ioq_rx.error =
						HAMMER2_IOQ_ERROR_KEYXCHGFAIL;
			if (RSA_public_decrypt(blksize, buf, ptr,
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

	if (DebugOpt) {
		fprintf(stderr, "Remote data: %s\n", handrx.quickmsg);
	}
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
