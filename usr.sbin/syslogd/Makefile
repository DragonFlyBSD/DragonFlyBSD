#	@(#)Makefile	8.1 (Berkeley) 6/6/93
# $FreeBSD: head/usr.sbin/syslogd/Makefile 211055 2010-08-08 02:45:14Z imp $

.PATH: ${.CURDIR}/../../usr.bin/wall

PROG=	syslogd
MAN=	syslog.conf.5 syslogd.8
SRCS=	syslogd.c ttymsg.c

DPADD=	${LIBUTIL}
LDADD=	-lutil

CFLAGS+= -DINET6
CFLAGS+= -I${.CURDIR}/../../usr.bin/wall

.include <bsd.prog.mk>
