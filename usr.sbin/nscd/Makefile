# $FreeBSD: src/usr.sbin/nscd/Makefile,v 1.5 2007/11/20 02:07:29 jb Exp $

PROG=	nscd
MAN=	nscd.conf.5 nscd.8

WARNS?=	2
CFLAGS+=-fno-strict-aliasing
SRCS=	agent.c cachelib.c cacheplcs.c config.c debug.c log.c \
	mp_rs_query.c mp_ws_query.c nscd.c nscdcli.c parser.c \
	protocol.c query.c singletons.c
CFLAGS+= -DCONFIG_PATH="\"${PREFIX}/etc/nscd.conf\""
DPADD=	${LIBM} ${LIBPTHREAD} ${LIBUTIL}
LDADD=	-lm -lpthread -lutil

.PATH: ${.CURDIR}/agents
.include "${.CURDIR}/agents/Makefile.inc"
.include <bsd.prog.mk>
