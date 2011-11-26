# $FreeBSD: src/usr.sbin/cron/cron/Makefile,v 1.16 2007/06/17 17:25:53 yar Exp $
# $DragonFly: src/usr.sbin/cron/cron/Makefile,v 1.3 2008/01/07 14:11:23 matthias Exp $

PROG=	cron
SRCS=	cron.c database.c do_command.c job.c user.c popen.c
CFLAGS+=-DLOGIN_CAP -DPAM
DPADD=	${LIBCRON} ${LIBPAM} ${LIBUTIL}
LDADD=	${LIBCRON} -lpam -lutil
MAN=	cron.8

.include <bsd.prog.mk>
