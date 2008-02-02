/*
 * Copyright (c) 2008 The DragonFly Project.  All rights reserved.
 *
 * This code is derived from software contributed to The DragonFly Project
 * by Matthias Schmidt <matthias@dragonflybsd.org>, University of Marburg,
 * Germany.
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
 *
 * $DragonFly: src/libexec/dma/crypto.c,v 1.1 2008/02/02 18:20:51 matthias Exp $
 */

#ifdef HAVE_CRYPTO

#include <openssl/x509.h>
#include <openssl/ssl.h>
#include <openssl/err.h>
#include <openssl/pem.h>
#include <openssl/rand.h>

#include <syslog.h>

#include "dma.h"

extern struct config *config;

static int
init_cert_file(struct qitem *it, SSL_CTX *ctx, const char *path)
{
	int error;

	/* Load certificate into ctx */
	error = SSL_CTX_use_certificate_chain_file(ctx, path);
	if (error < 1) {
		syslog(LOG_ERR, "%s: SSL: Cannot load certificate: %s",
			it->queueid, path);
		return (-1);
	}

	/* Add private key to ctx */
	error = SSL_CTX_use_PrivateKey_file(ctx, path, SSL_FILETYPE_PEM);
	if (error < 1) {
		syslog(LOG_ERR, "%s: SSL: Cannot load private key: %s",
			it->queueid, path);
		return (-1);
	}

	/*
	 * Check the consistency of a private key with the corresponding
         * certificate
	 */
	error = SSL_CTX_check_private_key(ctx);
	if (error < 1) {
		syslog(LOG_ERR, "%s: SSL: Cannot check private key: %s",
			it->queueid, path);
		return (-1);
	}

	return (0);
}

int
smtp_init_crypto(struct qitem *it, int fd, int feature)
{
	SSL_CTX *ctx = NULL;
	SSL_METHOD *meth = NULL;
	X509 *cert;
	char buf[2048];
	int error;

	/* Init SSL library */
	SSL_library_init();

	meth = TLSv1_client_method();

	ctx = SSL_CTX_new(meth);
	if (ctx == NULL) {
		syslog(LOG_ERR, "%s: remote delivery deferred:"
		       " SSL init failed: %m", it->queueid);
		return (2);
	}

	/* User supplied a certificate */
	if (config->certfile != NULL)
		init_cert_file(it, ctx, config->certfile);

	/*
	 * If the user wants STARTTLS, we have to send EHLO here
	 */
	if (((feature & SECURETRANS) != 0) &&
	     (feature & STARTTLS) != 0) {
		/* TLS init phase, disable SSL_write */
		config->features |= TLSINIT;

		send_remote_command(fd, "EHLO %s", hostname());
		if (check_for_smtp_error(fd, buf) == 0) {
			send_remote_command(fd, "STARTTLS");
			if (check_for_smtp_error(fd, buf) < 0) {
				syslog(LOG_ERR, "%s: remote delivery failed:"
				  " STARTTLS not available: %m", it->queueid);
				config->features &= ~TLSINIT;
				return (-1);
			}
		}
		/* End of TLS init phase, enable SSL_write/read */
		config->features &= ~TLSINIT;
	}

	config->ssl = SSL_new(ctx);
	if (config->ssl == NULL) {
		syslog(LOG_ERR, "%s: remote delivery deferred:"
		       " SSL struct creation failed:", it->queueid);
		return (2);
	}

	/* Set ssl to work in client mode */
	SSL_set_connect_state(config->ssl);

	/* Set fd for SSL in/output */
	error = SSL_set_fd(config->ssl, fd);
	if (error == 0) {
		error = SSL_get_error(config->ssl, error);
		syslog(LOG_ERR, "%s: remote delivery deferred:"
		       " SSL set fd failed (%d): %m", it->queueid, error);
		return (2);
	}

	/* Open SSL connection */
	error = SSL_connect(config->ssl);
	if (error < 0) {
		syslog(LOG_ERR, "%s: remote delivery failed:"
		       " SSL handshake fataly failed: %m", it->queueid);
		return (-1);
	}

	/* Get peer certificate */
	cert = SSL_get_peer_certificate(config->ssl);
	if (cert == NULL) {
		syslog(LOG_ERR, "%s: remote delivery deferred:"
		       " Peer did not provied certificate: %m", it->queueid);
	}
	X509_free(cert);

	return (0);
}

#if 0
/*
 * CRAM-MD5 authentication
 *
 * XXX TODO implement me, I don't have a mail server with CRAM-MD5 available
 */
int
smtp_auth_md5(int fd, char *login, char *password)
{
}
#endif /* 0 */

#endif /* HAVE_CRYPTO */
