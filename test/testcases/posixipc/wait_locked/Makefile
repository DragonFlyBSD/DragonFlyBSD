PROG=	wait_locked
SRCS=	wait_locked.c common.c
NO_MAN=
LDADD=  -lpthread

CFLAGS= -I ${.CURDIR}/../common
WARNS?=	3

.include <bsd.prog.mk>
.PATH: ${.CURDIR}/../common
