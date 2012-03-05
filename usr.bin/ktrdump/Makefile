# $FreeBSD: src/usr.bin/ktrdump/Makefile,v 1.3 2002/06/06 11:27:03 ru Exp $

PROG=	ktrdump
DPADD=	${LIBKVM} ${LIBEVTR} ${LIBDEVINFO}
LDADD=	-lkvm -levtr -ldevinfo
MAN=	ktrdump.8

.include <bsd.prog.mk>
