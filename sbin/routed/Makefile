# $FreeBSD: src/sbin/routed/Makefile,v 1.8.2.1 2001/04/25 10:58:50 ru Exp $
# $DragonFly: src/sbin/routed/Makefile,v 1.4 2006/10/17 00:55:43 pavalos Exp $

PROG=   routed
SRCS=   if.c input.c main.c output.c parms.c radix.c rdisc.c table.c trace.c
MAN=   routed.8
SUBDIR= rtquery
LDADD+= -lmd
DPADD+= ${LIBMD}

.include <bsd.prog.mk>
