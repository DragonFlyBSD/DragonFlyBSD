#	$OpenBSD: src/usr.sbin/rdate/Makefile,v 1.7 2003/11/20 23:23:09 avsm Exp $
#	$DragonFly: src/usr.sbin/rdate/Makefile,v 1.1 2004/12/01 15:04:43 joerg Exp $

PROG=	rdate
SRCS=	rdate.c rfc868time.c ntp.c ntpleaps.c
DPADD+= ${LIBUTIL}
LDADD+= -lutil
WARNS?=	6

MAN=	rdate.8

.include <bsd.prog.mk>
