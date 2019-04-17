#	$OpenBSD: Makefile,v 1.10 2003/11/20 23:23:09 avsm Exp $

PROG=	authpf
MAN=	authpf.8
BINOWN=	root
BINGRP=	authpf
BINMODE=	6555

.PATH:	${.CURDIR}/../pfctl
CFLAGS+=	-I${.CURDIR}/../pfctl

DPADD= ${LIBM} ${LIBUTIL}
LDADD= -lm -lutil

CLEANFILES+=	y.tab.h

.include <bsd.prog.mk>
