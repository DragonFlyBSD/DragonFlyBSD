#	$OpenBSD: Makefile,v 1.10 2003/11/20 23:23:09 avsm Exp $
# $DragonFly: src/usr.sbin/authpf/Makefile,v 1.2 2005/02/12 03:06:23 joerg Exp $

PROG=	authpf
MAN=	authpf.8
BINOWN=	root
BINGRP=	authpf
BINMODE=	6555
SRCS=	authpf.c

.PATH:	${.CURDIR}/../pfctl
CFLAGS+=	-I${.CURDIR}/../pfctl

LDADD+= -lm -lmd -lutil
DPADD+= ${LIBM}

CLEANFILES+=	y.tab.h

.include <bsd.prog.mk>
