# $DragonFly: src/usr.sbin/ntpd/Attic/Makefile,v 1.2 2005/02/28 21:34:48 joerg Exp $

CONTRIBDIR=	${.CURDIR}/../../contrib/ntpd
PATCHES!=	echo ${.CURDIR}/patches/*.patch
.PATH: ${CONTRIBDIR}

PROG=	ntpd
SRCS=	ntpd.c buffer.c log.c imsg.c ntp.c ntp_msg.c parse.y config.c
SRCS+=	server.c client.c util.c ${PATCHES}

CFLAGS+=	-I${CONTRIBDIR} -g

WARNS?=	6
MAN=	ntpd.8 ntpd.conf.5

.include <bsd.prog.mk>
