# $FreeBSD: src/sbin/ipfstat/Makefile,v 1.4.2.7 2002/04/27 18:13:13 darrenr Exp $
# $DragonFly: src/sbin/ipfstat/Makefile,v 1.3 2003/08/08 04:18:39 dillon Exp $

.PATH:	${.CURDIR}/../../contrib/ipfilter ${.CURDIR}/../../contrib/ipfilter/man

PROG=	ipfstat
MAN=	ipfstat.8
SRCS=	fils.c parse.c opt.c kmem.c facpri.c common.c printstate.c
CFLAGS+=-DUSE_INET6 -DIPL_NAME=\"/dev/ipl\" -DSTATETOP
CFLAGS+= -I${.CURDIR}/../../sys/contrib/ipfilter
CFLAGS+=-I${.CURDIR}/../../sys
CFLAGS+=-I${.CURDIR}/../../contrib/ipfilter

DPADD+=	${LIBNCURSES} ${LIBKVM}
LDADD+=	-lncurses -lkvm

MAINTAINER=	guido@freebsd.org

.include <bsd.prog.mk>
