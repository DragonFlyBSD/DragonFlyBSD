# $FreeBSD: src/usr.sbin/tzsetup/Makefile,v 1.10.2.1 2001/04/25 12:11:07 ru Exp $

PROG=	tzsetup
MAN=	tzsetup.8
CFLAGS+=	-I${.CURDIR}
LDADD+=	-ldialog -lncurses
DPADD+=	${LIBDIALOG} ${LIBNCURSES}
WARNS?=	2

.include <bsd.prog.mk>
