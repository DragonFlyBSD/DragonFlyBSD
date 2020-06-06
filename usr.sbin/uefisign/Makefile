# $FreeBSD: head/usr.sbin/uefisign/Makefile 359510 2020-04-01 15:10:26Z trasz $

PROG=	uefisign
SRCS=	uefisign.c child.c pe.c
MAN=	uefisign.8

LDADD=	-lprivate_crypto
DPADD=	${LIBCRYPTO}
LDFLAGS+= ${PRIVATELIB_LDFLAGS}
CFLAGS+= -I${.CURDIR}/../../crypto/libressl/include

.include <bsd.prog.mk>
