/* $FreeBSD: src/kerberos5/include/crypto-headers.h,v 1.1.2.2 2003/02/13 20:15:26 nectar Exp $ */
/* $DragonFly: src/kerberos5/include/crypto-headers.h,v 1.2 2003/06/17 04:26:17 dillon Exp $ */
#ifndef __crypto_headers_h__
#define __crypto_headers_h__
#define OPENSSL_DES_LIBDES_COMPATIBILITY
#include <openssl/des.h>
#include <openssl/rc4.h>
#include <openssl/md4.h>
#include <openssl/md5.h>
#include <openssl/sha.h>
#endif /* __crypto_headers_h__ */
