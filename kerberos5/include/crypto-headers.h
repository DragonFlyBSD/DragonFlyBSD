/* $FreeBSD: src/kerberos5/include/crypto-headers.h,v 1.2 2003/01/21 14:08:24 nectar Exp $ */
/* $DragonFly: src/kerberos5/include/crypto-headers.h,v 1.3 2005/01/16 14:25:46 eirikn Exp $ */
#ifndef __crypto_headers_h__
#define __crypto_headers_h__
#define OPENSSL_DES_LIBDES_COMPATIBILITY
#include <openssl/des.h>
#include <openssl/rc4.h>
#include <openssl/md4.h>
#include <openssl/md5.h>
#include <openssl/sha.h>
#endif /* __crypto_headers_h__ */
