# $FreeBSD: src/usr.bin/netstat/Makefile,v 1.14.2.3 2001/08/10 09:07:08 ru Exp $
#	@(#)Makefile	8.1 (Berkeley) 6/12/93

PROG=	netstat
SRCS=	if.c inet.c inet6.c main.c mbuf.c mroute.c route.c \
	unix.c netgraph.c mroute6.c ipsec.c # iso.c tp_astring.c
CFLAGS+=-DIPSEC
CFLAGS+=-DINET6

#CFLAGS+=-g
#.PATH:	${.CURDIR}/../../sys/netiso
BINGRP=	kmem
BINMODE=2555
DPADD=	${LIBKVM} ${LIBKINFO} ${LIBNETGRAPH} ${LIBUTIL}
LDADD=	-lkvm -lkinfo -lnetgraph -lutil

.include <bsd.prog.mk>
