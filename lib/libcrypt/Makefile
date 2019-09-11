#
# $FreeBSD: src/lib/libcrypt/Makefile,v 1.24.2.4 2001/07/16 03:28:26 peter Exp $
#

SHLIB_MAJOR=	4
LIB=		crypt
SHLIBDIR?=	/lib

SRCS=		blowfish.c crypt.c crypt-blowfish.c crypt-des.c crypt-md5.c \
		crypt-sha256.c crypt-sha512.c crypt-xsi.c \
		deprecated-crypt-sha256.c deprecated-crypt-sha512.c \
		misc.c
WARNS?=		2
NO_STRICT_ALIASING=

MAN=		crypt.3
MLINKS=		crypt.3 crypt_get_format.3 crypt.3 crypt_set_format.3
MLINKS+=	crypt.3 encrypt.3 crypt.3 setkey.3
CFLAGS+=	-I${.CURDIR}/../omd -I${.CURDIR}/../libutil
CFLAGS+=	-I${.CURDIR}/../../crypto/libressl/include
CFLAGS+=	-I${.CURDIR} -DHAS_DES -DHAS_BLOWFISH
# And the auth_getval() code and support.
.PATH:		${.CURDIR}/../libutil
SRCS+=		auth.c property.c
.for sym in auth_getval property_find properties_read properties_free
CFLAGS+=	-D${sym}=_libcrypt_${sym}
.endfor
PRECIOUSLIB=	yes

.include <bsd.lib.mk>
