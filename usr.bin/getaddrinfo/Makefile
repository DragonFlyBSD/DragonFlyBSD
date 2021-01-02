#	$NetBSD: Makefile,v 1.2 2014/04/29 01:21:02 christos Exp $

PROG=	getaddrinfo
SRCS=	getaddrinfo.c tables.h

DPADD=	${LIBUTIL}
LDADD=	-lutil

SYS_SOCKET_H?=	${.CURDIR}/../../sys/sys/socket.h

CFLAGS+=	-I${.OBJDIR}
CLEANFILES+=	tables.h
tables.h: tables.awk ${SYS_SOCKET_H}
	LC_ALL=C awk -f ${.ALLSRC} > ${.TARGET}

.include <bsd.prog.mk>
