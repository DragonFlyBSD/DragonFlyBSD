#	@(#)Makefile	8.1 (Berkeley) 6/4/93
# $FreeBSD: src/libexec/makekey/Makefile,v 1.11.2.1 2001/04/25 10:40:52 ru Exp $
# $DragonFly: src/libexec/makekey/Makefile,v 1.2 2003/06/17 04:27:07 dillon Exp $

PROG=	makekey
MAN=	makekey.8

DPADD+= ${LIBCRYPT}
LDADD+= -lcrypt

.include <bsd.prog.mk>
