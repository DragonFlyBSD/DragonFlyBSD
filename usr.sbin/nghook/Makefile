# $FreeBSD: src/usr.sbin/nghook/Makefile,v 1.2.4.1 2001/04/25 12:10:22 ru Exp $
# $DragonFly: src/usr.sbin/nghook/Makefile,v 1.2 2003/06/17 04:29:57 dillon Exp $
# $Whistle: Makefile,v 1.4 1999/01/16 04:44:33 archie Exp $

PROG=		nghook
SRCS=		main.c
MAN=		nghook.8
DPADD=		${LIBNETGRAPH}
LDADD=		-lnetgraph
COPTS+=		-Wall

.include <bsd.prog.mk>
