#	@(#)Makefile	8.1 (Berkeley) 6/4/93
# $FreeBSD: src/libexec/talkd/Makefile,v 1.6.2.2 2001/10/18 12:30:42 des Exp $

.PATH:  ${.CURDIR}/../../usr.bin/wall ${.CURDIR}/../../usr.bin/who

PROG=	ntalkd
SRCS=	talkd.c announce.c process.c table.c print.c ttymsg.c utmpentry.c
MAN=	talkd.8

DPADD=	${LIBUTIL}
LDADD=	-lutil

CFLAGS+=-I${.CURDIR}/../../usr.bin/wall
CFLAGS+=-I${.CURDIR}/../../usr.bin/who -DSUPPORT_UTMP -DSUPPORT_UTMPX

.include <bsd.prog.mk>
