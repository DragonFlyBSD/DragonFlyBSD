#	@(#)Makefile	8.1 (Berkeley) 6/4/93
# $FreeBSD: src/libexec/talkd/Makefile,v 1.6.2.2 2001/10/18 12:30:42 des Exp $

.PATH:  ${.CURDIR}/../../usr.bin/wall

PROG=	ntalkd
SRCS=	talkd.c announce.c process.c table.c print.c ttymsg.c
MAN=	talkd.8

CFLAGS+=-I${.CURDIR}/../../usr.bin/wall

.include <bsd.prog.mk>
