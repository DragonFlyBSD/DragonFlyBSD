#	$OpenBSD: Makefile,v 1.10 2003/11/20 23:23:09 avsm Exp $
# $DragonFly: src/usr.sbin/authpf/Makefile,v 1.1 2004/09/21 21:25:28 joerg Exp $

PROG=	authpf
MAN=	authpf.8
BINOWN=	root
BINGRP=	authpf
BINMODE=	6555
SRCS=	authpf.c parse.y pfctl_parser.c pf_print_state.c
SRCS+=	pfctl_radix.c pfctl_osfp.c
#SRCS+=	pfctl_altq.c
WARNS?=	6

.PATH:	${.CURDIR}/../pfctl
CFLAGS+=	-I${.CURDIR}/../pfctl

LDADD+= -lm -lmd
DPADD+= ${LIBM}

CLEANFILES+=	y.tab.h

.include <bsd.prog.mk>
