#	@(#)Makefile	8.1 (Berkeley) 6/6/93
# $FreeBSD: src/usr.bin/lock/Makefile,v 1.6 1999/12/18 13:55:14 peter Exp $
# $DragonFly: src/usr.bin/lock/Makefile,v 1.3 2005/01/05 00:45:31 cpressey Exp $

PROG=	lock
WARNS?=	6
BINMODE=4555
DPADD=	${LIBCRYPT}
LDADD=	-lcrypt

.include <bsd.prog.mk>
