/*
 * Sun RPC is a product of Sun Microsystems, Inc. and is provided for
 * unrestricted use provided that this legend is included on all tape
 * media and as a part of the software program in whole or part.  Users
 * may copy or modify Sun RPC without charge, but are not authorized
 * to license or distribute it to anyone else except as part of a product or
 * program developed by the user or with the express written consent of
 * Sun Microsystems, Inc.
 *
 * SUN RPC IS PROVIDED AS IS WITH NO WARRANTIES OF ANY KIND INCLUDING THE
 * WARRANTIES OF DESIGN, MERCHANTIBILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE, OR ARISING FROM A COURSE OF DEALING, USAGE OR TRADE PRACTICE.
 *
 * Sun RPC is provided with no support and without any obligation on the
 * part of Sun Microsystems, Inc. to assist in its use, correction,
 * modification or enhancement.
 *
 * SUN MICROSYSTEMS, INC. SHALL HAVE NO LIABILITY WITH RESPECT TO THE
 * INFRINGEMENT OF COPYRIGHTS, TRADE SECRETS OR ANY PATENTS BY SUN RPC
 * OR ANY PART THEREOF.
 *
 * In no event will Sun Microsystems, Inc. be liable for any lost revenue
 * or profits or other special, indirect and consequential damages, even if
 * Sun has been advised of the possibility of such damages.
 *
 * Sun Microsystems, Inc.
 * 2550 Garcia Avenue
 * Mountain View, California  94043
 *
 * @(#)generic.c 1.2 91/03/11 Copyr 1986 Sun Micro
 * $FreeBSD: src/usr.bin/newkey/generic.c,v 1.3.2.1 2001/07/04 22:32:20 kris Exp $
 */

/*
 * Copyright (C) 1986, Sun Microsystems, Inc.
 */

#include <sys/file.h>

#include <err.h>
#include <stdio.h>
#include <stdlib.h>

#include <openssl/bn.h>
#include <openssl/crypto.h>
#include <openssl/err.h>

#include <rpc/rpc.h>
#include <rpc/key_prot.h>

#include "externs.h"

static void	adjust(char[], char *);
static BIGNUM	*itobn(long i);

/*
 * Generate a seed
 */
static void
getseed(char *seed, int seedsize, unsigned char *pass)
{
	int i;

	for (i = 0; i < seedsize; i++) {
		seed[i] = (arc4random() & 0xff) ^ pass[i % 8];
	}
}

static BIGNUM *
itobn(long i)
{
	BIGNUM *n = NULL;

	if ((n = BN_new()) == NULL)
		errx(1, "could not create BIGNUM: %s",
		     ERR_error_string(ERR_get_error(), 0));
	BN_init(n);
	if (i > 0)
		BN_add_word(n, (u_long)i);
	else
		BN_sub_word(n, (u_long)(-i));
	return(n);
}

/*
 * Generate a random public/secret key pair
 */
void
genkeys(char *public, char *secret, char *pass)
{
#define	BASEBITS (8*sizeof (short) - 1)
#define	BASE (short)(1 << BASEBITS)

	unsigned int i;
	short r;
	unsigned short seed[KEYSIZE/BASEBITS + 1];
	char *xkey;

	BN_CTX *ctx;
	BIGNUM *pk, *sk, *tmp, *base, *root, *modulus;

	pk = itobn(0);
	sk = itobn(0);
	tmp = itobn(0);
	base = itobn(BASE);
	root = itobn(PROOT);
	modulus = NULL;
	if (BN_hex2bn(&modulus, HEXMODULUS) == 0)
		errx(1, "could not convert modulus to BIGNUM: %s",
		     ERR_error_string(ERR_get_error(), 0));

	if ((ctx = BN_CTX_new()) == NULL)
		errx(1, "could not create BN_CTX: %s",
		     ERR_error_string(ERR_get_error(), 0));

	getseed((char *)seed, sizeof (seed), (u_char *)pass);
	for (i = 0; i < KEYSIZE/BASEBITS + 1; i++) {
		r = seed[i] % BASE;
		BN_zero(tmp);
		BN_add_word(tmp, r);
		BN_mul(sk, base, sk, ctx);
		BN_add(sk, tmp, sk);
	}
	BN_zero(tmp);
	BN_div(tmp, sk, sk, modulus, ctx);
	BN_mod_exp(pk, root, sk, modulus, ctx);

	if ((xkey = BN_bn2hex(sk)) == NULL)
		errx(1, "could convert sk to hex: %s",
		     ERR_error_string(ERR_get_error(), 0));
	adjust(secret, xkey);
	OPENSSL_free(xkey);

	if ((xkey = BN_bn2hex(pk)) == NULL)
		errx(1, "could convert pk to hex: %s",
		     ERR_error_string(ERR_get_error(), 0));
	adjust(public, xkey);
	OPENSSL_free(xkey);

	BN_free(base);
	BN_free(modulus);
	BN_free(pk);
	BN_free(sk);
	BN_free(root);
	BN_free(tmp);
}

/*
 * Adjust the input key so that it is 0-filled on the left
 */
static void
adjust(char keyout[HEXKEYBYTES+1], char *keyin)
{
	char *p;
	char *s;

	for (p = keyin; *p; p++)
		;
	for (s = keyout + HEXKEYBYTES; p >= keyin; p--, s--) {
		*s = *p;
	}
	while (s >= keyout) {
		*s-- = '0';
	}
}
