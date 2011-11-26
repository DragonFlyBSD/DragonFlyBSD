/*
 * Copyright (c) 2011 Alex Hornung <alex@alexhornung.com>.
 * All rights reserved.
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
#include <sys/types.h>
#include <sys/param.h>
#include <sys/ioctl.h>
#include <sys/sysctl.h>
#include <crypto/cryptodev.h>

#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <stdio.h>

#include "tcplay.h"

static
int
getallowsoft(void)
{
	int old;
	size_t olen;

	olen = sizeof(old);

	if (sysctlbyname("kern.cryptodevallowsoft", &old, &olen, NULL, 0) < 0) {
		perror("accessing sysctl kern.cryptodevallowsoft failed");
	}

	return old;
}

static
void
setallowsoft(int new)
{
	int old;
	size_t olen, nlen;

	olen = nlen = sizeof(new);

	if (sysctlbyname("kern.cryptodevallowsoft", &old, &olen, &new, nlen) < 0) {
		perror("accessing sysctl kern.cryptodevallowsoft failed");
	}
}

static
int
get_cryptodev_cipher_id(struct tc_crypto_algo *cipher)
{
	if	(strcmp(cipher->name, "AES-128-XTS") == 0)
		return CRYPTO_AES_XTS;
	else if (strcmp(cipher->name, "AES-256-XTS") == 0)
		return CRYPTO_AES_XTS;
	else if (strcmp(cipher->name, "TWOFISH-128-XTS") == 0)
		return CRYPTO_TWOFISH_XTS;
	else if (strcmp(cipher->name, "TWOFISH-256-XTS") == 0)
		return CRYPTO_TWOFISH_XTS;
	else if (strcmp(cipher->name, "SERPENT-128-XTS") == 0)
		return CRYPTO_SERPENT_XTS;
	else if (strcmp(cipher->name, "SERPENT-256-XTS") == 0)
		return CRYPTO_SERPENT_XTS;
	else
		return -1;
}

int
syscrypt(struct tc_crypto_algo *cipher, unsigned char *key, size_t klen, unsigned char *iv,
    unsigned char *in, unsigned char *out, size_t len, int do_encrypt)
{
	struct session_op session;
	struct crypt_op cryp;
	int cipher_id;
	int cryptodev_fd = -1, fd = -1;

	cipher_id = get_cryptodev_cipher_id(cipher);
	if (cipher_id < 0) {
		tc_log(1, "Cipher %s not found\n",
		    cipher->name);
		return ENOENT;
	}

	if ((cryptodev_fd = open("/dev/crypto", O_RDWR, 0)) < 0) {
		perror("Could not open /dev/crypto");
		goto err;
	}
	if (ioctl(cryptodev_fd, CRIOGET, &fd) == -1) {
		perror("CRIOGET failed");
		goto err;
	}
	memset(&session, 0, sizeof(session));
	session.cipher = cipher_id;
	session.key = (caddr_t) key;
	session.keylen = klen;
	if (ioctl(fd, CIOCGSESSION, &session) == -1) {
		perror("CIOCGSESSION failed");
		goto err;
	}
	memset(&cryp, 0, sizeof(cryp));
	cryp.ses = session.ses;
	cryp.op = do_encrypt ? COP_ENCRYPT : COP_DECRYPT;
	cryp.flags = 0;
	cryp.len = len;
	cryp.src = (caddr_t) in;
	cryp.dst = (caddr_t) out;
	cryp.iv = (caddr_t) iv;
	cryp.mac = 0;
	if (ioctl(fd, CIOCCRYPT, &cryp) == -1) {
		perror("CIOCCRYPT failed");
		goto err;
	}
	if (ioctl(fd, CIOCFSESSION, &session.ses) == -1) {
		perror("CIOCFSESSION failed");
		goto err;
	}
	close(fd);
	close(cryptodev_fd);
	return (0);

err:
	if (fd != -1)
		close(fd);
	if (cryptodev_fd != -1)
		close(cryptodev_fd);
	return (-1);
}

int
tc_crypto_init(void)
{
	int allowed;

	allowed = getallowsoft();
	if (allowed == 0)
		setallowsoft(1);

	return 0;
}

