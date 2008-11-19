# $FreeBSD: src/libexec/rpc.rquotad/Makefile,v 1.4.2.1 2001/04/25 10:40:56 ru Exp $
# $DragonFly: src/libexec/rpc.rquotad/Makefile,v 1.3 2008/11/19 18:41:30 swildner Exp $

PROG =	rpc.rquotad
SRCS =	rquotad.c
WARNS?=	6

MAN  =	rpc.rquotad.8

DPADD=	${LIBRPCSVC}
LDADD=	-lrpcsvc

.include <bsd.prog.mk>
