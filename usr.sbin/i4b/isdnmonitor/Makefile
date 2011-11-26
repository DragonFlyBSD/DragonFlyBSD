# $FreeBSD: src/usr.sbin/i4b/isdnmonitor/Makefile,v 1.2.2.3 2001/08/01 17:29:17 obrien Exp $
# $DragonFly: src/usr.sbin/i4b/isdnmonitor/Makefile,v 1.2 2003/06/17 04:29:55 dillon Exp $

PROG=	isdnmonitor
SRCS=	main.c curses.c
MAN=	isdnmonitor.8

# compile debug support
COPTS+=	-DDEBUG

DPADD=	${LIBCURSES}
LDADD=	-lcurses

.include <bsd.prog.mk>
