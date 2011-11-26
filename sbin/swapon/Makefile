#	@(#)Makefile	8.1 (Berkeley) 6/5/93
# $FreeBSD: src/sbin/swapon/Makefile,v 1.2.14.2 2001/08/01 09:06:16 obrien Exp $
# $DragonFly: src/sbin/swapon/Makefile,v 1.4 2006/10/17 00:55:44 pavalos Exp $

PROG=	swapon
MAN=	swapon.8
LINKS=	${BINDIR}/swapon ${BINDIR}/swapoff
LINKS+= ${BINDIR}/swapon ${BINDIR}/swapctl
MLINKS= swapon.8 swapoff.8
MLINKS+=swapon.8 swapctl.8

DPADD=  ${LIBUTIL}
LDADD=	-lutil

.include <bsd.prog.mk>
