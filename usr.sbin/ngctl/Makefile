# $FreeBSD: src/usr.sbin/ngctl/Makefile,v 1.4.2.3 2002/02/01 18:17:43 archie Exp $
# $DragonFly: src/usr.sbin/ngctl/Makefile,v 1.3 2005/03/16 05:19:11 joerg Exp $
# $Whistle: Makefile,v 1.3 1999/01/16 00:10:11 archie Exp $

PROG=		ngctl
SRCS=		main.c mkpeer.c config.c connect.c name.c show.c list.c \
		msg.c debug.c shutdown.c rmhook.c status.c types.c write.c
MAN=		ngctl.8
DPADD+=		${LIBNETGRAPH}
LDADD+=		-lnetgraph
COPTS+=		-Wall
WARNS?=		6

.include <bsd.prog.mk>
