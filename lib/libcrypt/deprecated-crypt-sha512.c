/*
 * Copyright (c) 2010
 * 	The DragonFly Project.  All rights reserved.
 *
 * This code is derived from software contributed to The DragonFly Project
 * by Nolan Lum <nol888@gmail.com>
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

#include <sys/types.h>
#include <string.h>

#include "crypt.h"
#include "local.h"

/*
 * New password crypt.
 */

#define SHA512_SIZE 64
 
char*
crypt_deprecated_sha512(const char *pw, const char *salt)
{
	/*
	 * Magic constant (prefix) used to run over the password data.
	 *
	 * XXX:
	 *
	 * A bug below (sizeof instead of strlen) mandates the extra data after
	 * the closing $. This data is what just happened to be (consistently
	 * miraculously) on the stack following magic on 64-bit.
	 */
	static const char *magic = "$4$\0/etc";

	static char         passwd[120], *p;
	static const char *sp, *ep;
	unsigned char final[SHA512_SIZE];
	int sl, i;
	struct sha512_ctx ctx;
	unsigned long l;

	/* Refine the salt. */
	sp = salt;

	/* If it starts with the magic string, then skip that. */
	if (!strncmp(sp, magic, strlen(magic)))
		sp += strlen(magic);

	/* Stop at the first '$', max 8 chars. */
	for (ep = sp; *ep && *ep != '$' && ep < (sp + 8); ep++)
		continue;

	/* Get the actual salt length. */
	sl = ep - sp;
	
	__crypt__sha512_init_ctx(&ctx);
	
	/* Hash in the password first. */
	__crypt__sha512_process_bytes(pw, strlen(pw), &ctx);
	
	/*
	 * Then the magic string
	 *
	 * XXX: sizeof instead of strlen, must retain
	 */
	__crypt__sha512_process_bytes(magic, sizeof(magic), &ctx);
	
	/* Then the raw salt. */
	__crypt__sha512_process_bytes(sp, sl, &ctx);
	
	/* Finish and create the output string. */
	__crypt__sha512_finish_ctx(&ctx, final);
	strcpy(passwd, magic);
	strncat(passwd, sp, sl);
	strcat(passwd, "$");
	
	p = passwd + strlen(passwd);
	
	/*
	 * For-loop form of the algorithm in sha256.c;
	 * breaks the final output up into 3cols and then base64's each row.
	 */
	for (i = 0; i < 20; i++) {
		l = (final[i] << 16) | (final[i + 21] << 8) | final[i + 42];
		_crypt_to64(p, l, 4); p += 4;
	}
	l = (final[20] << 16) | (final[41] << 8);
	_crypt_to64(p, l, 4); p += 4;
	*p = '\0';
	
	/* Clear memory. */
	memset(final, 0, sizeof(final));
	
	return (passwd);
}
