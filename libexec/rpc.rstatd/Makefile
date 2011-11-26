# $FreeBSD: src/libexec/rpc.rstatd/Makefile,v 1.5.2.1 2001/04/25 10:40:56 ru Exp $

PROG =	rpc.rstatd
SRCS =	rstatd.c rstat_proc.c
MAN =	rpc.rstatd.8
WARNS?=	2

DPADD=	${LIBRPCSVC} ${LIBUTIL} ${LIBDEVSTAT} ${LIBKINFO}
LDADD=	-lrpcsvc -lutil -ldevstat -lkinfo

.include <bsd.prog.mk>
