# $FreeBSD: src/usr.sbin/nghook/Makefile,v 1.2.4.1 2001/04/25 12:10:22 ru Exp $
# $DragonFly: src/usr.sbin/nghook/Makefile,v 1.3 2005/03/16 05:27:27 joerg Exp $
# $Whistle: Makefile,v 1.4 1999/01/16 04:44:33 archie Exp $

PROG=		nghook
SRCS=		main.c
MAN=		nghook.8
DPADD=		${LIBNETGRAPH}
LDADD=		-lnetgraph
COPTS+=		-Wall
WARNS?=		6

.include <bsd.prog.mk>
