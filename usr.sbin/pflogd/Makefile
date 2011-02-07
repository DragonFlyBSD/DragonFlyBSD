#	$OpenBSD: Makefile,v 1.6 2003/11/20 23:23:09 avsm Exp $
#	$DragonFly: src/usr.sbin/pflogd/Makefile,v 1.2 2008/11/03 00:25:45 pavalos Exp $

LDADD+= -lpcap -lutil
DPAPP+=	${LIBPCAP} ${LIBUTIL}
CFLAGS+= -DHAVE_SNPRINTF

PROG=	pflogd
SRCS=	pflogd.c privsep.c privsep_fdpass.c
MAN=	pflogd.8

.include <bsd.prog.mk>
