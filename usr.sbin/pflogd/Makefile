#	$OpenBSD: Makefile,v 1.6 2003/11/20 23:23:09 avsm Exp $

LDADD+= -lpcap -lutil
DPADD+=	${LIBPCAP} ${LIBUTIL}
CFLAGS+= -DHAVE_SNPRINTF -I${.CURDIR}/../../contrib/libpcap
WARNS?=	3

PROG=	pflogd
SRCS=	pflogd.c privsep.c privsep_fdpass.c
MAN=	pflogd.8

.include <bsd.prog.mk>
