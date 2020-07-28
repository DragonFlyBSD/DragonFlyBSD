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
#ifdef OPENSSL_HAS_ECC
#include <openssl/ecdsa.h>
#endif
#include <openssl/dh.h>

int ssh_compatible_openssl(long, long);
void ssh_libcrypto_init(void);

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

#endif /* WITH_OPENSSL */
#endif /* _OPENSSL_COMPAT_H */
