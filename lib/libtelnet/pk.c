/*-
 * Copyright (c) 1991, 1993
 *      Dave Safford.  All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Neither the name of the University nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE REGENTS AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE REGENTS OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 * 
 *
 * $FreeBSD: src/crypto/telnet/libtelnet/pk.c,v 1.2.2.4 2002/08/24 07:28:35 nsayer Exp $
 */

/* public key routines */
/* functions:
	genkeys(char *public, char *secret)
	common_key(char *secret, char *public, desData *deskey)
        pk_encode(char *in, *out, DesData *deskey);
        pk_decode(char *in, *out, DesData *deskey);
      where
	char public[HEXKEYBYTES + 1];
	char secret[HEXKEYBYTES + 1];
 */

#include <sys/time.h>
#include <err.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <openssl/bn.h>
#include <openssl/crypto.h>
#include <openssl/des.h>
#include <openssl/err.h>

#include "pk.h"
 
static void adjust(char keyout[HEXKEYBYTES+1], char *keyin);

/*
 * Choose top 128 bits of the common key to use as our idea key.
 */
static void
extractideakey(BIGNUM *ck, IdeaData *ideakey)
{
        BIGNUM *a, *z;
        int i;
        BN_ULONG r, base = (1 << 8);
        char *k;

	if ((z = BN_new()) == NULL)
		errx(1, "could not create BIGNUM");
	BN_zero(z);
	if ((a = BN_new()) == NULL)
		errx(1, "could not create BIGNUM");
	BN_zero(a);
	BN_add(a, ck, z);
        for (i = 0; i < ((KEYSIZE - 128) / 8); i++) {
		r = BN_div_word(a, base);
        }
        k = (char *)ideakey;
        for (i = 0; i < 16; i++) {
                r = BN_div_word(a, base);
                *k++ = r;
        }
	BN_free(z);
	BN_free(a);
}

/*
 * Choose middle 64 bits of the common key to use as our des key, possibly
 * overwriting the lower order bits by setting parity. 
 */
static void
extractdeskey(BIGNUM *ck, DesData *deskey)
{
        BIGNUM *a, *z;
        int i;
        BN_ULONG r, base = (1 << 8);
        char *k;

	if ((z = BN_new()) == NULL)
		errx(1, "could not create BIGNUM");
	BN_zero(z);
	if ((a = BN_new()) == NULL)
		errx(1, "could not create BIGNUM");
	BN_zero(a);
	BN_add(a, ck, z);
        for (i = 0; i < ((KEYSIZE - 64) / 2) / 8; i++) {
		r = BN_div_word(a, base);
        }
        k = (char *)deskey;
        for (i = 0; i < 8; i++) {
		r = BN_div_word(a, base);
                *k++ = r;
        }
	BN_free(z);
	BN_free(a);
}

/*
 * get common key from my secret key and his public key
 */
void
common_key(char *xsecret, char *xpublic, IdeaData *ideakey, DesData *deskey)
{
        BIGNUM *public, *secret, *common, *modulus;
	BN_CTX *ctx;

	if ((ctx = BN_CTX_new()) == NULL)
		errx(1, "could not create BN_CTX");
	modulus = NULL;
	if (BN_hex2bn(&modulus, HEXMODULUS) == 0)
		errx(1, "could not convert modulus");
	public = NULL;
	if (BN_hex2bn(&public, xpublic) == 0)
		errx(1, "could not convert public");
	secret = NULL;
	if (BN_hex2bn(&secret, xsecret) == 0)
		errx(1, "could not convert secret");

	if ((common = BN_new()) == NULL)
		errx(1, "could not create BIGNUM");
	BN_zero(common);
	BN_mod_exp(common, public, secret, modulus, ctx);
        extractdeskey(common, deskey);
        extractideakey(common, ideakey);
	DES_set_odd_parity(deskey);
	BN_free(common);
	BN_free(secret);
	BN_free(public);
	BN_free(modulus);
	BN_CTX_free(ctx);
}

/*
 * Generate a seed
 */
static void
getseed(char *seed, int seedsize)
{
	int i;

	for (i = 0; i < seedsize; i++) {
		seed[i] = arc4random() & 0xff;
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
genkeys(char *public, char *secret)
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

	getseed((char *)seed, sizeof (seed));
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

static char hextab[17] = "0123456789ABCDEF";

/* given a DES key, cbc encrypt and translate input to terminated hex */
void
pk_encode(char *in, char *out, DesData *key)
{
	char buf[256];
	DesData i;
	DES_key_schedule k;
	int l,op,deslen;

	memset(&i,0,sizeof(i));
	memset(buf,0,sizeof(buf));
	deslen = ((strlen(in) + 7)/8)*8;
	DES_key_sched(key, &k);
	DES_cbc_encrypt(in,buf,deslen, &k,&i,DES_ENCRYPT);
	for (l=0,op=0;l<deslen;l++) {
		out[op++] = hextab[(buf[l] & 0xf0) >> 4];
		out[op++] = hextab[(buf[l] & 0x0f)];
	}
	out[op] = '\0';
}

/* given a DES key, translate input from hex and decrypt */
void
pk_decode(char *in, char *out, DesData *key)
{
	char buf[256];
	DesData i;
	DES_key_schedule k;
	int n1,n2,op;
	size_t l;

	memset(&i,0,sizeof(i));
	memset(buf,0,sizeof(buf));
	for (l=0,op=0;l<strlen(in)/2;l++,op+=2) {
		if (in[op] > '9')
			n1 = in[op] - 'A' + 10;
		else
			n1 = in[op] - '0';
		if (in[op+1] > '9')
			n2 = in[op+1] - 'A' + 10;
		else
			n2 = in[op+1] - '0';
		buf[l] = n1*16 +n2;
	}
	DES_key_sched(key, &k);
	DES_cbc_encrypt(buf,out,strlen(in)/2, &k,&i,DES_DECRYPT);
	out[strlen(in)/2] = '\0';
}
