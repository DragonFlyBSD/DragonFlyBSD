# $FreeBSD: head/usr.sbin/efivar/Makefile 326472 2017-12-02 15:25:45Z imp $

PROG=	efivar
MAN=	efivar.8

LDADD=	-lefivar
DPADD=	${LIBEFIVAR}

SRCS=	efivar.c efiutil.c

CFLAGS+=	-I${.CURDIR}/../../lib/libefivar

.include <bsd.prog.mk>
