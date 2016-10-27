#
# $FreeBSD: src/secure/lib/libcipher/Makefile,v 1.10.2.1 2002/07/19 18:46:27 ru Exp $
# $DragonFly: src/secure/lib/libcipher/Makefile,v 1.2 2003/06/17 04:27:48 dillon Exp $
#

LIB=		cipher
SRCS=		crypt.c
PRECIOUSLIB=	yes
#NOPROFILE=	yes
WARNS?=		2

MAN=	cipher.3
MLINKS= cipher.3 encrypt.3 cipher.3 setkey.3
MLINKS+=cipher.3 des_cipher.3 cipher.3 des_setkey.3

#SUBDIR=		test

.include <bsd.lib.mk>
