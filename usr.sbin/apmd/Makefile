# $FreeBSD: src/usr.sbin/apmd/Makefile,v 1.2.2.2 2001/04/25 12:09:18 ru Exp $
# $DragonFly: src/usr.sbin/apmd/Makefile,v 1.4 2008/10/29 18:18:04 swildner Exp $

PROG=	apmd
SRCS=	apmd.c apmdlex.l apmdparse.y y.tab.h
WARNS?=	3

DPADD=	${LIBL} ${LIBUTIL}
LDADD=	-ll -lutil

MAN=	apmd.8

YFLAGS+=-v
CFLAGS+=-I. -I${.CURDIR} #-DYY_STACK_USED
# for debug:
#CFLAGS+= -g -DDEBUG

CLEANFILES= y.output

test:
	./apmd -d -f etc/apmd.conf -n

.include <bsd.prog.mk>
