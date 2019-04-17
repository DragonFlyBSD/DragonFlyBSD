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
#include <unistd.h>
#include <openssl/md5.h>

#include <stdio.h>		/* for FILE in extern.h */
#include <netinet/in.h>		/* for struct sockaddr_in */
#include "extern.h"

char *
sitemd5(const char *filename, char * const buf)
{
	unsigned char digest[MD5_DIGEST_LENGTH];
	static const char hex[]="0123456789abcdef";
	MD5_CTX ctx;
	unsigned char buffer[4096];
	struct stat st;
	off_t size;
	int fd, bytes, i, saved_errno;

	if (!buf)
		return NULL;

	fd = open(filename, O_RDONLY);
	if (fd < 0)
		return NULL;
	if (fstat(fd, &st) < 0) {
		bytes = -1;
		goto err;
	}

	MD5_Init(&ctx);
	size = st.st_size;
	bytes = 0;
	while (size > 0) {
		if ((size_t)size > sizeof(buffer))
			bytes = read(fd, buffer, sizeof(buffer));
		else
			bytes = read(fd, buffer, size);
		if (bytes < 0)
			break;
		MD5_Update(&ctx, buffer, bytes);
		size -= bytes;
	}

err:
	saved_errno = errno;
	close(fd);
	errno = saved_errno;

	if (bytes < 0)
		return NULL;

	MD5_Final(digest, &ctx);
	for (i = 0; i < MD5_DIGEST_LENGTH; i++) {
		buf[2*i] = hex[digest[i] >> 4];
		buf[2*i+1] = hex[digest[i] & 0x0f];
	}
	buf[MD5_DIGEST_LENGTH * 2] = '\0';

	return buf;
}
