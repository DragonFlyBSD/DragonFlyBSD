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
#include <sha512.h>
#include "crypt.h"

/*
 * New password crypt.
 */
 
char*
crypt_sha512(const char *pw, const char *salt)
{
	static const char *magic = "$4$"; /* Magic string for this
										 * algorithm. Easier to change
										 * when factored as constant.
										 */
	static char         passwd[120], *p;
	static const char *sp, *ep;
	unsigned char final[SHA512_SIZE];
	int sl, i;
	SHA512_CTX ctx;
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
	
	SHA512_Init(&ctx);
	
	/* Hash in the password first. */
	SHA512_Update(&ctx, pw, strlen(pw));
	
	/* Then the magic string */
	SHA512_Update(&ctx, magic, sizeof(magic));
	
	/* Then the raw salt. */
	SHA512_Update(&ctx, sp, sl);
	
	/* Finish and create the output string. */
	SHA512_Final(final, &ctx);
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
