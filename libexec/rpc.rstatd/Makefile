# $FreeBSD: src/libexec/rpc.rstatd/Makefile,v 1.5.2.1 2001/04/25 10:40:56 ru Exp $
# $DragonFly: src/libexec/rpc.rstatd/Makefile,v 1.2 2003/06/17 04:27:07 dillon Exp $

PROG =	rpc.rstatd
SRCS =	rstatd.c rstat_proc.c
MAN =	rpc.rstatd.8

DPADD=	${LIBRPCSVC} ${LIBUTIL} ${LIBKVM} ${LIBDEVSTAT}
LDADD=	-lrpcsvc -lutil -lkvm -ldevstat

.include <bsd.prog.mk>
