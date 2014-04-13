#
# $FreeBSD: src/lib/libcrypt/Makefile,v 1.24.2.4 2001/07/16 03:28:26 peter Exp $
# $DragonFly: src/lib/libcrypt/Makefile,v 1.8 2008/10/28 17:23:45 swildner Exp $
#

SHLIB_MAJOR=	4
LIB=		crypt
SHLIBDIR?=	/lib

.PATH:		${.CURDIR}/../libmd
SRCS=		crypt.c crypt-md5.c \
		crypt-sha256.c crypt-sha512.c \
		deprecated-crypt-sha256.c deprecated-crypt-sha512.c \
		md5c.c sha256c.c sha512c.c misc.c
WARNS?=		2
MAN=		crypt.3
MLINKS=		crypt.3 crypt_get_format.3 crypt.3 crypt_set_format.3
CFLAGS+=	-I${.CURDIR}/../libmd -I${.CURDIR}/../libutil
CFLAGS+=	-D_CTYPE_H_DISABLE_MACROS_ 
# Pull in the crypt-des.c source, assuming it is present.
.if exists(${.CURDIR}/../../secure/lib/libcrypt/crypt-des.c) && \
    !defined(NO_CRYPT)
.PATH:		${.CURDIR}/../../secure/lib/libcrypt
SRCS+=		crypt-des.c crypt-blowfish.c blowfish.c
CFLAGS+=	-I${.CURDIR} -DHAS_DES -DHAS_BLOWFISH
.endif
# And the auth_getval() code and support.
.PATH:		${.CURDIR}/../libutil
SRCS+=		auth.c property.c
.for sym in MD5Init MD5Final MD5Update MD5Pad auth_getval \
	    property_find properties_read properties_free \
	    SHA256_Init SHA256_Update SHA256_Final \
	    SHA512_Init SHA512_Update SHA512_Final
CFLAGS+=	-D${sym}=__${sym}
.endfor
PRECIOUSLIB=	yes

.include <bsd.lib.mk>
