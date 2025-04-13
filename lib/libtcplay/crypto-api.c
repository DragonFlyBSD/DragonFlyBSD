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

#include <crypto/cryptoapi/cryptoapi.h>

#include <errno.h>
#include <string.h>
#include <stdio.h>

#include "tcplay.h"

static
cryptoapi_cipher_t
get_cryptoapi_cipher(struct tc_crypto_algo *cipher)
{
	if	(strcmp(cipher->name, "AES-128-XTS") == 0)
		return cryptoapi_cipher_find("aes-xts", 128);
	else if (strcmp(cipher->name, "AES-256-XTS") == 0)
		return cryptoapi_cipher_find("aes-xts", 256);
	else if (strcmp(cipher->name, "TWOFISH-128-XTS") == 0)
		return cryptoapi_cipher_find("twofish-xts", 128);
	else if (strcmp(cipher->name, "TWOFISH-256-XTS") == 0)
		return cryptoapi_cipher_find("twofish-xts", 256);
	else if (strcmp(cipher->name, "SERPENT-128-XTS") == 0)
		return cryptoapi_cipher_find("serpent-xts", 128);
	else if (strcmp(cipher->name, "SERPENT-256-XTS") == 0)
		return cryptoapi_cipher_find("serpent-xts", 256);
	else
		return NULL;
}

int
syscrypt(struct tc_crypto_algo *cipher, unsigned char *key, size_t klen, unsigned char *iv ,
    unsigned char *in, unsigned char *out, size_t len, int do_encrypt)
{
	int error;
	cryptoapi_cipher_session_t session;
	cryptoapi_cipher_t crypto_cipher;

	bzero(&session, sizeof(session));

	crypto_cipher = get_cryptoapi_cipher(cipher);
	if (crypto_cipher == NULL) {
		tc_log(1, "Cipher %s not found\n",
		    cipher->name);
		return (ENOENT);
	}

	error = cryptoapi_cipher_initsession(crypto_cipher, &session);
	if (error) {
		perror("Failed to init crypto session");
		goto err;
	}

	error = cryptoapi_cipher_setkey(&session, key, klen);
	if (error) {
		perror("Failed to set crypto key");
		goto err;
	}

	if (in != out)
		memcpy(in, out, len);

	if (do_encrypt)
		error = cryptoapi_cipher_encrypt(&session, out, len,
				(struct cryptoapi_cipher_iv *)iv);
	else
		error = cryptoapi_cipher_decrypt(&session, out, len,
				(struct cryptoapi_cipher_iv *)iv);

	if (error) {
		perror("Failed to encrypt/decrypt");
		goto err;
	}

err:
	cryptoapi_cipher_freesession(&session);
	return (-1);
}

int
tc_crypto_init(void)
{
	return 0;
}

