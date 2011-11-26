#	@(#)Makefile	8.1 (Berkeley) 6/6/93
# $DragonFly: src/usr.bin/users/Makefile,v 1.3 2007/08/27 16:51:00 pavalos Exp $
.PATH: ${.CURDIR}/../../usr.bin/who

PROG=	users
SRCS=	users.c utmpentry.c

DPADD=	${LIBUTIL}
LDADD=	-lutil

CFLAGS+= -I${.CURDIR}/../../usr.bin/who -DSUPPORT_UTMP -DSUPPORT_UTMPX

.include <bsd.prog.mk>
