# $FreeBSD: src/sbin/gpt/Makefile,v 1.7 2005/09/01 02:49:20 marcel Exp $

PROG=	gpt
SRCS=	add.c create.c destroy.c gpt.c label.c map.c migrate.c recover.c \
	remove.c show.c boot.c expand.c
MAN=	gpt.8

LDADD+=	-lutil
DPADD+=	${LIBUTIL}

.include <bsd.prog.mk>
