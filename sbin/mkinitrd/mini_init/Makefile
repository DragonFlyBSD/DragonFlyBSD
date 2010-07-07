#	@(#)Makefile	8.1 (Berkeley) 7/19/93
# $FreeBSD: src/sbin/init/Makefile,v 1.20.2.4 2001/08/01 06:37:01 obrien Exp $
# $DragonFly: src/sbin/init/Makefile,v 1.5 2006/10/17 00:55:41 pavalos Exp $

PROG=	oinit
NOSHARED=yes
NOMAN=
BINMODE=500
BINDIR=${SHAREDIR}/initrd/sbin
DPADD=	${LIBUTIL}
LDADD=	-lutil

.include <bsd.prog.mk>
