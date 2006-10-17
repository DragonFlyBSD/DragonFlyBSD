#$FreeBSD: src/sbin/ipfstat/Makefile,v 1.4.2.7 2002/04/27 18:13:13 darrenr Exp $
# $DragonFly: src/sbin/ipfstat/Makefile,v 1.6 2006/10/17 00:55:41 pavalos Exp $

.PATH:	${.CURDIR}/../../contrib/ipfilter ${.CURDIR}/../../contrib/ipfilter/man

PROG=	ipfstat
MAN=	ipfstat.8
SRCS=	fils.c parse.c opt.c kmem.c facpri.c common.c printstate.c
CFLAGS+=-DUSE_INET6 -DIPL_NAME=\"/dev/ipl\" -DSTATETOP
CFLAGS+=-I${.CURDIR}/../../sys/contrib/ipfilter
CFLAGS+=-I${.CURDIR}/../../sys/contrib/ipfilter/netinet
CFLAGS+=-I${.CURDIR}/../../contrib/ipfilter
WARNS?=	0

DPADD+=	${LIBNCURSES} ${LIBKVM}
LDADD+=	-lncurses -lkvm

.include <bsd.prog.mk>
