# $FreeBSD: src/libexec/rpc.rwalld/Makefile,v 1.4.2.1 2001/04/25 10:40:57 ru Exp $
# $DragonFly: src/libexec/rpc.rwalld/Makefile,v 1.2 2003/06/17 04:27:07 dillon Exp $

PROG =	rpc.rwalld
SRCS =	rwalld.c
MAN =	rpc.rwalld.8

DPADD=	${LIBRPCSVC} ${LIBUTIL}
LDADD=	-lrpcsvc -lutil

.include <bsd.prog.mk>
