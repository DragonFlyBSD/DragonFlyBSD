/*-
 * Copyright 2005 Colin Percival
 * All rights reserved.
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
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 * $FreeBSD: src/lib/libmd/sha256.h,v 1.2 2006/01/17 15:35:56 phk Exp $
 * $DragonFly: src/lib/libmd/sha256.h,v 1.2 2008/09/11 20:25:34 swildner Exp $
 */

#ifndef _SHA512_H_
#define _SHA512_H_

#include <sys/types.h>
#define SHA384_BLOCK_LENGTH		128
#define SHA384_DIGEST_LENGTH		48
#define SHA384_DIGEST_STRING_LENGTH	(SHA384_DIGEST_LENGTH * 2 + 1)
#define SHA512_BLOCK_LENGTH		128
#define SHA512_DIGEST_LENGTH		64
#define SHA512_DIGEST_STRING_LENGTH	(SHA512_DIGEST_LENGTH * 2 + 1)

typedef struct _SHA512_CTX {
	u_int64_t	state[8];
	u_int64_t	bitcount[2];
	u_int8_t	buffer[SHA512_BLOCK_LENGTH];
} SHA512_CTX;

typedef SHA512_CTX SHA384_CTX;

__BEGIN_DECLS
int	SHA384_Init(SHA384_CTX *);
void	SHA384_Update(SHA384_CTX *, const u_int8_t *, size_t);
void	SHA384_Final(u_int8_t[SHA384_DIGEST_LENGTH], SHA384_CTX *);
int	SHA512_Init(SHA512_CTX*);
void	SHA512_Update(SHA512_CTX*, const void *, size_t);
void	SHA512_Final(unsigned char [SHA512_DIGEST_LENGTH], SHA512_CTX *);
char*	SHA512_End(SHA512_CTX *, char *);
char   *SHA512_Data(const void *, unsigned int, char *);
char   *SHA512_File(const char *, char *);
char   *SHA512_FileChunk(const char *, char *, off_t, off_t);
__END_DECLS

#endif /* !_SHA512_H_ */
