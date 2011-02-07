#	@(#)Makefile	8.1 (Berkeley) 6/6/93
# $FreeBSD: src/usr.sbin/syslogd/Makefile,v 1.14 2008/12/17 16:51:40 obrien Exp $
# $DragonFly: src/usr.sbin/syslogd/Makefile,v 1.3 2004/08/09 20:11:19 dillon Exp $

.PATH: ${.CURDIR}/../../usr.bin/wall ${.CURDIR}/../nscd ${.CURDIR}/../../usr.bin/who

PROG=	syslogd
MAN=	syslog.conf.5 syslogd.8
SRCS=	pidfile.c syslogd.c ttymsg.c utmpentry.c

DPADD=	${LIBUTIL}
LDADD=	-lutil

CFLAGS+= -DINET6
CFLAGS+= -I${.CURDIR}/../../usr.bin/wall
CFLAGS+= -I${.CURDIR}/../../usr.bin/who -DSUPPORT_UTMP -DSUPPORT_UTMPX


.include <bsd.prog.mk>
