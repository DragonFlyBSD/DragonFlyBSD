#	@(#)Makefile	8.1 (Berkeley) 6/6/93
.PATH: ${.CURDIR}/../who

PROG=	users
SRCS=	users.c utmpentry.c

DPADD=	${LIBUTIL}
LDADD=	-lutil

CFLAGS+= -I${.CURDIR}/../who -DSUPPORT_UTMP -DSUPPORT_UTMPX

.include <bsd.prog.mk>
