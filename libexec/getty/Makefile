#	from: @(#)Makefile	8.1 (Berkeley) 6/4/93
# $FreeBSD: head/libexec/getty/Makefile 329724 2018-02-21 15:57:24Z trasz $

PROG=	getty
SRCS=	main.c init.c subr.c chat.c
DPADD=	${LIBUTIL}
LDADD=	-lutil
MAN=	gettytab.5 ttys.5 getty.8
WARNS?=	2

.include <bsd.prog.mk>

