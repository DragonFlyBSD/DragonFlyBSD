# $FreeBSD: src/libexec/rpc.rstatd/Makefile,v 1.5.2.1 2001/04/25 10:40:56 ru Exp $
# $DragonFly: src/libexec/rpc.rstatd/Makefile,v 1.3 2004/12/22 11:01:49 joerg Exp $

PROG =	rpc.rstatd
SRCS =	rstatd.c rstat_proc.c
MAN =	rpc.rstatd.8

DPADD=	${LIBRPCSVC} ${LIBUTIL} ${LIBDEVSTAT} ${LIBKINFO}
LDADD=	-lrpcsvc -lutil -ldevstat -lkinfo

.include <bsd.prog.mk>
