PROG=	wait_unlocked
SRCS=	wait_unlocked.c common.c
NOMAN=
LDADD=  -lpthread

CFLAGS= -I ${.CURDIR}/../common
WARNS?=	3

.include <bsd.prog.mk>
.PATH: ${.CURDIR}/../common
