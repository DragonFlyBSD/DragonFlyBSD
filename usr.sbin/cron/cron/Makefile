# $FreeBSD: src/usr.sbin/cron/cron/Makefile,v 1.16 2007/06/17 17:25:53 yar Exp $

PROG=	cron
SRCS=	cron.c database.c do_command.c job.c user.c popen.c
CFLAGS+=-DLOGIN_CAP -DPAM
DPADD=	${LIBCRON} ${LIBPAM} ${LIBUTIL}
LDADD=	${LIBCRON} ${MINUSLPAM} -lutil
LDFLAGS+= ${LDFLAGSPAM}
MAN=	cron.8

.include <bsd.prog.mk>
