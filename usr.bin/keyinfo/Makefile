#	@(#)Makefile	5.5 (Berkeley) 7/1/90
# $FreeBSD: src/usr.bin/keyinfo/Makefile,v 1.4.6.2 2001/11/06 03:36:33 imp Exp $
# $DragonFly: src/usr.bin/keyinfo/Makefile,v 1.3 2005/01/16 21:14:51 cpressey Exp $

PROG=	keyinfo
DPADD=	${LIBSKEY} ${LIBCRYPT} ${LIBMD}
LDADD=	-lskey -lcrypt -lmd
BINMODE=4555
WARNS?=	6

.include <bsd.prog.mk>
