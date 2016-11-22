# $FreeBSD: head/usr.sbin/uefisign/Makefile 279323 2015-02-26 15:48:20Z trasz $

PROG=	uefisign
SRCS=	uefisign.c child.c pe.c
MAN=	uefisign.8

LDADD=	-lprivate_crypto
DPADD=	${LIBRECRYPTO}
LDFLAGS+= -rpath /lib/priv -L ${_SHLIBDIRPREFIX}/usr/lib/priv

.include <bsd.prog.mk>
