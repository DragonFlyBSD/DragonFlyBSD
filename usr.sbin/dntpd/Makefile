# $DragonFly: src/usr.sbin/dntpd/Makefile,v 1.2 2005/04/27 00:42:13 dillon Exp $

PROG=	dntpd
SRCS=	main.c socket.c ntpreq.c convert.c log.c client.c system.c

LDADD += -lm

WARNS?=	6
MAN= dntpd.8

.include <bsd.prog.mk>
