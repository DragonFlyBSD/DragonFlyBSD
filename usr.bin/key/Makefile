#	@(#)Makefile	5.6 (Berkeley) 3/5/91
# $FreeBSD: src/usr.bin/key/Makefile,v 1.4.6.1 2001/04/25 11:29:23 ru Exp $
# $DragonFly: src/usr.bin/key/Makefile,v 1.2 2003/06/17 04:29:27 dillon Exp $

PROG=	key
SRCS=	skey.c

CFLAGS+= -D_SKEY_INTERNAL

DPADD=	${LIBSKEY} ${LIBCRYPT} ${LIBMD}
LDADD=	-lskey -lcrypt -lmd

.include <bsd.prog.mk>
