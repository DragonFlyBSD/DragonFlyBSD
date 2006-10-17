# $FreeBSD: src/sbin/ipfs/Makefile,v 1.1.2.1 2001/11/14 14:21:03 guido Exp $
# $DragonFly: src/sbin/ipfs/Makefile,v 1.4 2006/10/17 00:55:41 pavalos Exp $

.PATH:  ${.CURDIR}/../../contrib/ipfilter ${.CURDIR}/../../contrib/ipfilter/man

PROG=   ipfs
MAN=    ipfs.8
SRCS=   ipfs.c
CFLAGS+=-DUSE_INET6 -DIPL_NAME=\"/dev/ipl\"
CFLAGS+=-I${.CURDIR}/../../sys/contrib/ipfilter/netinet
CFLAGS+=-I${.CURDIR}/../../contrib/ipfilter
WARNS?=	0

.include <bsd.prog.mk>
