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
#include <sys/sysctl.h>

#include <crypto/cryptodev.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

int syscrypt_cryptodev(
    const char *cipher_name, unsigned char *key, size_t klen, unsigned char *iv,
    unsigned char *in, unsigned char *out, size_t len, int do_encrypt,
    int repetitions);

static int get_cryptodev_cipher_id(const char *cipher_name)
{
	if (strcmp(cipher_name, "AES-CBC") == 0)
		return CRYPTO_AES_CBC;
	else if (strcmp(cipher_name, "AES-XTS") == 0)
		return CRYPTO_AES_XTS;
	else if (strcmp(cipher_name, "SERPENT-CBC") == 0)
		return CRYPTO_SERPENT_CBC;
	else if (strcmp(cipher_name, "SERPENT-XTS") == 0)
		return CRYPTO_SERPENT_XTS;
	else if (strcmp(cipher_name, "TWOFISH-CBC") == 0)
		return CRYPTO_TWOFISH_CBC;
	else if (strcmp(cipher_name, "TWOFISH-XTS") == 0)
		return CRYPTO_TWOFISH_XTS;
	else
		return -1;
}

int syscrypt_cryptodev(
    const char *cipher_name, unsigned char *key, size_t klen, unsigned char *iv,
    unsigned char *in, unsigned char *out, size_t len, int do_encrypt,
    int repetitions)
{
	struct session_op session;
	struct crypt_op cryp;
	int cipher_id;
	int cryptodev_fd = -1, fd = -1;

	cipher_id = get_cryptodev_cipher_id(cipher_name);
	if (cipher_id < 0) {
		printf("Cipher %s not found\n", cipher_name);
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
	session.key = (caddr_t)key;
	session.keylen = klen;
	if (ioctl(fd, CIOCGSESSION, &session) == -1) {
		perror("CIOCGSESSION failed");
		goto err;
	}
	for (int i = 0; i < repetitions; ++i) {
		memset(&cryp, 0, sizeof(cryp));
		cryp.ses = session.ses;
		cryp.op = do_encrypt ? COP_ENCRYPT : COP_DECRYPT;
		cryp.flags = 0;
		cryp.len = len;
		cryp.src = (caddr_t)in;
		cryp.dst = (caddr_t)out;
		cryp.iv = (caddr_t)iv;
		cryp.mac = 0;
		if (ioctl(fd, CIOCCRYPT, &cryp) == -1) {
			perror("CIOCCRYPT failed");
			goto err;
		}
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
