/*
 * Copyright (c) 2005 Darren Tucker <dtucker@zip.com.au>
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF MIND, USE, DATA OR PROFITS, WHETHER
 * IN AN ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING
 * OUT OF OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#ifndef _OPENSSL_COMPAT_H
#define _OPENSSL_COMPAT_H

#include "includes.h"
#ifdef WITH_OPENSSL

#include <openssl/opensslv.h>
#include <openssl/crypto.h>
#include <openssl/evp.h>
#include <openssl/rsa.h>
#include <openssl/dsa.h>
#include <openssl/ecdsa.h>
#include <openssl/dh.h>

int ssh_compatible_openssl(long, long);
void ssh_libcrypto_init(void);

#if (OPENSSL_VERSION_NUMBER < 0x1000100fL)
# error OpenSSL 1.0.1 or greater is required
#endif

#ifndef OPENSSL_VERSION
# define OPENSSL_VERSION	SSLEAY_VERSION
#endif

#if OPENSSL_VERSION_NUMBER < 0x10000001L
# define LIBCRYPTO_EVP_INL_TYPE unsigned int
#else
# define LIBCRYPTO_EVP_INL_TYPE size_t
#endif

#ifndef OPENSSL_RSA_MAX_MODULUS_BITS
# define OPENSSL_RSA_MAX_MODULUS_BITS	16384
#endif
#ifndef OPENSSL_DSA_MAX_MODULUS_BITS
# define OPENSSL_DSA_MAX_MODULUS_BITS	10000
#endif

#if defined(HAVE_EVP_RIPEMD160)
# if defined(OPENSSL_NO_RIPEMD) || defined(OPENSSL_NO_RMD160)
#  undef HAVE_EVP_RIPEMD160
# endif
#endif

/* LibreSSL/OpenSSL 1.1x API compat */
#if 0
/* wrong checks? */
#ifndef DSA_SIG_GET0
void DSA_SIG_get0(const DSA_SIG *sig, const BIGNUM **pr, const BIGNUM **ps);
#endif /* DSA_SIG_GET0 */

#ifndef DSA_SIG_SET0
int DSA_SIG_set0(DSA_SIG *sig, BIGNUM *r, BIGNUM *s);
#endif /* DSA_SIG_SET0 */

#ifndef HAVE_EVP_MD_CTX_new
EVP_MD_CTX *EVP_MD_CTX_new(void);
#endif /* HAVE_EVP_MD_CTX_new */

#ifndef HAVE_EVP_MD_CTX_free
void EVP_MD_CTX_free(EVP_MD_CTX *ctx);
#endif /* HAVE_EVP_MD_CTX_free */
#endif

#endif /* WITH_OPENSSL */
#endif /* _OPENSSL_COMPAT_H */
