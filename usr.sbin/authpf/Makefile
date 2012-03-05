#	$OpenBSD: Makefile,v 1.10 2003/11/20 23:23:09 avsm Exp $

PROG=	authpf
MAN=	authpf.8
BINOWN=	root
BINGRP=	authpf
BINMODE=	6555
SRCS=	authpf.c

.PATH:	${.CURDIR}/../pfctl
CFLAGS+=	-I${.CURDIR}/../pfctl

LDADD+= -lm -lmd -lutil
DPADD+= ${LIBM} ${LIBMD} ${LIBUTIL}

CLEANFILES+=	y.tab.h

.include <bsd.prog.mk>
