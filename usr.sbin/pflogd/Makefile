#	$OpenBSD: Makefile,v 1.6 2003/11/20 23:23:09 avsm Exp $
#	$DragonFly: src/usr.sbin/pflogd/Makefile,v 1.1 2004/09/21 21:25:28 joerg Exp $

LDADD+= -lpcap -lutil
DPAPP+=	${LIBPCAP} ${LIBUTIL}

PROG=	pflogd
SRCS=	pflogd.c privsep.c privsep_fdpass.c
MAN=	pflogd.8
WARNS?=	6

.include <bsd.prog.mk>
