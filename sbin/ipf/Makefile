# $FreeBSD: src/sbin/ipf/Makefile,v 1.4.2.6 2002/04/27 18:11:34 darrenr Exp $
# $DragonFly: src/sbin/ipf/Makefile,v 1.4 2004/01/31 06:56:40 dillon Exp $

.PATH:	${.CURDIR}/../../contrib/ipfilter ${.CURDIR}/../../contrib/ipfilter/man

PROG=	ipf
MAN=	ipf.4 ipf.5 ipf.8
SRCS=	ipf.c parse.c opt.c facpri.c common.c
CFLAGS+=-DUSE_INET6 -DIPL_NAME=\"/dev/ipl\"
CFLAGS+= -I${.CURDIR}/../../sys/contrib/ipfilter
CFLAGS+=-I${.CURDIR}/../../contrib/ipfilter

.include <bsd.prog.mk>
