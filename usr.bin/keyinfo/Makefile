#	@(#)Makefile	5.5 (Berkeley) 7/1/90
# $FreeBSD: src/usr.bin/keyinfo/Makefile,v 1.4.6.2 2001/11/06 03:36:33 imp Exp $
# $DragonFly: src/usr.bin/keyinfo/Makefile,v 1.2 2003/06/17 04:29:27 dillon Exp $

PROG=	keyinfo
DPADD=	${LIBSKEY} ${LIBCRYPT} ${LIBMD}
LDADD=	-lskey -lcrypt -lmd
BINMODE=4555
WARNS?=1

.include <bsd.prog.mk>
