#	@(#)Makefile	8.2 (Berkeley) 4/17/94
# $FreeBSD: src/usr.bin/compress/Makefile,v 1.4.6.2 2002/07/16 00:45:05 tjr Exp $
# $DragonFly: src/usr.bin/compress/Makefile,v 1.3 2005/01/16 04:28:00 cpressey Exp $

PROG=	compress
WARNS?=	6
SRCS=	compress.c zopen.c
LINKS=	${BINDIR}/compress ${BINDIR}/uncompress
MLINKS=	compress.1 uncompress.1

# XXX zopen is not part of libc
# MAN=zopen.3

.include <bsd.prog.mk>
