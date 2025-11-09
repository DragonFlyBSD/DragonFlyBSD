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
#include <stdio.h>
#include <string.h>

int syscrypt_cryptoapi(
    const char *cipher_name, unsigned char *key, size_t klen, unsigned char *iv,
    unsigned char *in, unsigned char *out, size_t len, int do_encrypt,
    int repetitions);

static cryptoapi_cipher_t
get_cryptoapi_cipher(const char *cipher_name, size_t keysize_in_bits)
{
	if (strcmp(cipher_name, "AES-CBC") == 0)
		return cryptoapi_cipher_find("aes-cbc", keysize_in_bits);
	else if (strcmp(cipher_name, "AES-XTS") == 0)
		return cryptoapi_cipher_find("aes-xts", keysize_in_bits);
	else if (strcmp(cipher_name, "SERPENT-CBC") == 0)
		return cryptoapi_cipher_find("serpent-cbc", keysize_in_bits);
	else if (strcmp(cipher_name, "SERPENT-XTS") == 0)
		return cryptoapi_cipher_find("serpent-xts", keysize_in_bits);
	else if (strcmp(cipher_name, "TWOFISH-CBC") == 0)
		return cryptoapi_cipher_find("twofish-cbc", keysize_in_bits);
	else if (strcmp(cipher_name, "TWOFISH-XTS") == 0)
		return cryptoapi_cipher_find("twofish-xts", keysize_in_bits);
	else
		return NULL;
}

int syscrypt_cryptoapi(
    const char *cipher_name, unsigned char *key, size_t klen, unsigned char *iv,
    unsigned char *in, unsigned char *out, size_t len, int do_encrypt,
    int repetitions)
{
	int error;
	cryptoapi_cipher_session_t session;
	cryptoapi_cipher_t crypto_cipher;

	session = NULL;

	crypto_cipher = get_cryptoapi_cipher(cipher_name, klen * 8);
	if (crypto_cipher == NULL) {
		printf("Cipher %s not found\n", cipher_name);
		return (ENOENT);
	}

	session = cryptoapi_cipher_newsession(crypto_cipher);
	if (session == NULL) {
		printf("Failed to init crypto session\n");
		error = ENOMEM;
		goto err;
	}

	error = cryptoapi_cipher_setkey(session, key, klen);
	if (error) {
		printf("Failed to set crypto key: %s\n", strerror(error));
		goto err;
	}

	if (in != out)
		memcpy(out, in, len);

	for (int i = 0; i < repetitions; ++i) {
		if (do_encrypt)
			error = cryptoapi_cipher_encrypt(
			    session, out, len, iv, sizeof(cryptoapi_cipher_iv));
		else
			error = cryptoapi_cipher_decrypt(
			    session, out, len, iv, sizeof(cryptoapi_cipher_iv));
		if (error)
			break;
	}

	if (error) {
		printf("Failed to encrypt/decrypt: %s\n", strerror(error));
		goto err;
	}

	cryptoapi_cipher_freesession(session);
	return (0);

err:
	cryptoapi_cipher_freesession(session);
	return (error);
}
