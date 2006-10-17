# $FreeBSD: src/sbin/ipnat/Makefile,v 1.4.2.6 2002/04/27 18:12:33 darrenr Exp $
# $DragonFly: src/sbin/ipnat/Makefile,v 1.6 2006/10/17 00:55:41 pavalos Exp $

.PATH:	${.CURDIR}/../../contrib/ipfilter ${.CURDIR}/../../contrib/ipfilter/man

PROG=	ipnat
MAN=	ipnat.8 ipnat.4 ipnat.5
SRCS=	ipnat.c kmem.c natparse.c common.c printnat.c
CFLAGS+=-DUSE_INET6 -DIPL_NAME=\"/dev/ipl\"
CFLAGS+=-I${.CURDIR}/../../sys/contrib/ipfilter
CFLAGS+=-I${.CURDIR}/../../sys/contrib/ipfilter/netinet
CFLAGS+=-I${.CURDIR}/../../contrib/ipfilter
WARNS?=	0

DPADD+=	${LIBKVM}
LDADD+=  -lkvm

.include <bsd.prog.mk>
