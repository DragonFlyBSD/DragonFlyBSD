# $DragonFly: src/usr.sbin/ntpd/Attic/Makefile,v 1.1 2004/11/09 20:05:42 joerg Exp $

CONTRIBDIR=	${.CURDIR}/../../contrib/ntpd
PATCHES!=	echo ${.CURDIR}/patches/*.patch
.PATH: ${CONTRIBDIR}

PROG=	ntpd
SRCS=	ntpd.c buffer.c log.c imsg.c ntp.c ntp_msg.c parse.y config.c
SRCS+=	server.c client.c util.c ${PATCHES}

CFLAGS+=	-I${CONTRIBDIR}

WARNS?=	6
MAN=	ntpd.8 ntpd.conf.5

.include <bsd.prog.mk>