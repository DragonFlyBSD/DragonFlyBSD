/*
 * Copyright (c) 2019 The DragonFly Project.  All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
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
#include <sys/stat.h>
#include <errno.h>
#include <fcntl.h>
#include <stdlib.h>
#include <unistd.h>
#include <openssl/md5.h>
#include <openssl/sha.h>
#include <openssl/ripemd.h>

#include <stdio.h>		/* for FILE in mtree.h */
#include "extern.h"

/* max(MD5_DIGEST_LENGTH, SHA_DIGEST_LENGTH,
	SHA256_DIGEST_LENGTH, SHA512_DIGEST_LENGTH,
	RIPEMD160_DIGEST_LENGTH) * 2 + 1 */
#define	HEX_DIGEST_LENGTH 129

typedef union {
	MD5_CTX md5;
	SHA_CTX sha1;
	SHA256_CTX sha256;
	SHA512_CTX sha512;
	RIPEMD160_CTX ripemd160;
} DIGEST_CTX;

char *
dohash(int flag, const char *filename)
{
	unsigned char digest[HEX_DIGEST_LENGTH];
	static const char hex[]="0123456789abcdef";
	DIGEST_CTX context;
	void *ctx;
	unsigned char buffer[4096];
	char *buf;
	struct stat st;
	off_t size;
	int fd, bytes, i, digest_len;

	ctx = &context;

	if (flag == F_MD5)
		digest_len = MD5_DIGEST_LENGTH;
	else if (flag == F_RMD160)
		digest_len = RIPEMD160_DIGEST_LENGTH;
	else if (flag == F_SHA1)
		digest_len = SHA_DIGEST_LENGTH;
	else if (flag == F_SHA256)
		digest_len = SHA256_DIGEST_LENGTH;
	else if (flag == F_SHA384)
		digest_len = SHA384_DIGEST_LENGTH;
	else if (flag == F_SHA512)
		digest_len = SHA512_DIGEST_LENGTH;
	else
		return NULL;

	buf = malloc(digest_len * 2 + 1);
	if (!buf)
		return NULL;

	fd = open(filename, O_RDONLY);
	if (fd < 0)
		return NULL;
	if (fstat(fd, &st) < 0) {
		bytes = -1;
		goto err;
	}

	if (flag == F_MD5)
		MD5_Init(ctx);
	else if (flag == F_RMD160)
		RIPEMD160_Init(ctx);
	else if (flag == F_SHA1)
		SHA1_Init(ctx);
	else if (flag == F_SHA256)
		SHA256_Init(ctx);
	else if (flag == F_SHA384)
		SHA384_Init(ctx);
	else if (flag == F_SHA512)
		SHA512_Init(ctx);

	size = st.st_size;
	bytes = 0;
	while (size > 0) {
		if ((size_t)size > sizeof(buffer))
			bytes = read(fd, buffer, sizeof(buffer));
		else
			bytes = read(fd, buffer, size);
		if (bytes < 0)
			break;

		if (flag == F_MD5)
			MD5_Update(ctx, buffer, bytes);
		else if (flag == F_RMD160)
			RIPEMD160_Update(ctx, buffer, bytes);
		else if (flag == F_SHA1)
			SHA1_Update(ctx, buffer, bytes);
		else if (flag == F_SHA256)
			SHA256_Update(ctx, buffer, bytes);
		else if (flag == F_SHA384)
			SHA384_Update(ctx, buffer, bytes);
		else if (flag == F_SHA512)
			SHA512_Update(ctx, buffer, bytes);

		size -= bytes;
	}

err:
	close(fd);

	if (bytes < 0)
		return NULL;

	if (flag == F_MD5)
		MD5_Final(digest, ctx);
	else if (flag == F_RMD160)
		RIPEMD160_Final(digest, ctx);
	else if (flag == F_SHA1)
		SHA1_Final(digest, ctx);
	else if (flag == F_SHA256)
		SHA256_Final(digest, ctx);
	else if (flag == F_SHA384)
		SHA384_Final(digest, ctx);
	else if (flag == F_SHA512)
		SHA512_Final(digest, ctx);

	for (i = 0; i < digest_len; i++) {
		buf[2*i] = hex[digest[i] >> 4];
		buf[2*i+1] = hex[digest[i] & 0x0f];
	}
	buf[digest_len * 2] = '\0';

	return buf;
}
