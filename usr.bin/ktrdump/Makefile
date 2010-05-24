# $DragonFly: src/usr.bin/ktrdump/Makefile,v 1.3 2008/11/10 02:05:30 swildner Exp $
# $FreeBSD: src/usr.bin/ktrdump/Makefile,v 1.3 2002/06/06 11:27:03 ru Exp $

PROG=	ktrdump
DPADD=	${LIBKVM} ${LIBEVTR}
LDADD=	-lkvm -levtr -ldevinfo
MAN=	ktrdump.8

.include <bsd.prog.mk>
