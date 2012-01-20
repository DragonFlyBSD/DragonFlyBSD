/*
 * Copyright (c) 1999
 *      Mark Murray.  All rights reserved.
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
 * THIS SOFTWARE IS PROVIDED BY MARK MURRAY AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL MARK MURRAY OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 * $FreeBSD: src/lib/libcrypt/crypt.c,v 1.14.2.2 2001/05/24 12:20:02 markm Exp $
 * $DragonFly: src/lib/libcrypt/crypt.c,v 1.3 2005/08/04 17:27:09 drhodus Exp $
 */

#include <sys/types.h>
#include <string.h>
#include <libutil.h>
#include "crypt.h"

/*
 * XXX - NOTE:
 *
 * The deprecated sha256/512 functions are somehow sensitive to the
 * order of this crypt_types array as well as their respective "name" members.
 *
 * In order to ensure that both existing passwords will continue to work and
 * that new passwords will be more secure by using the new algorithms even
 * without updating the existing login.conf, this array is now scanned
 * backwards. This could be reverted in the future when the deprecated SHA
 * functionality is removed.
 */
static const struct {
	const char *const name;
	char *(*const func)(const char *, const char *);
	const char *const magic;
} crypt_types[] = {
#ifdef HAS_DES
	{
		"des",
		crypt_des,
		NULL
	},
#endif
	{
		"md5",
		crypt_md5,
		"$1$"
	},
#ifdef HAS_BLOWFISH
	{
		"blf",
		crypt_blowfish,
		"$2"
	},
#endif
	{
		"sha256",
		crypt_deprecated_sha256,
		"$3$"
	},
	{
		"sha512",
		crypt_deprecated_sha512,
		"$4$"
	},
	{
		"sha256",
		crypt_sha256,
		"$5$"
	},
	{
		"sha512",
		crypt_sha512,
		"$6$"
	}
};

static int crypt_type = -1;

static void
crypt_setdefault(void)
{
	char *def;
	int i;

	if (crypt_type != -1)
		return;
	def = auth_getval("crypt_default");
	if (def == NULL) {
		crypt_type = 0;
		return;
	}
	for (i = sizeof(crypt_types) / sizeof(crypt_types[0]) - 1; i >= 0; i--) {
		if (strcmp(def, crypt_types[i].name) == 0) {
			crypt_type = i;
			return;
		}
	}
	crypt_type = 0;
}

const char *
crypt_get_format(void)
{

	crypt_setdefault();
	return (crypt_types[crypt_type].name);
}

int
crypt_set_format(char *type)
{
	int i;

	crypt_setdefault();
	for (i = sizeof(crypt_types) / sizeof(crypt_types[0]) - 1; i >= 0; i--) {
		if (strcmp(type, crypt_types[i].name) == 0) {
			crypt_type = i;
			return (1);
		}
	}
	return (0);
}

char *
crypt(char *passwd, char *salt)
{
	int i;

	crypt_setdefault();
	for (i = sizeof(crypt_types) / sizeof(crypt_types[0]) - 1; i >= 0; i--) {
		if (crypt_types[i].magic != NULL && strncmp(salt,
		    crypt_types[i].magic, strlen(crypt_types[i].magic)) == 0)
			return (crypt_types[i].func(passwd, salt));
	}
	return (crypt_types[crypt_type].func(passwd, salt));
}
