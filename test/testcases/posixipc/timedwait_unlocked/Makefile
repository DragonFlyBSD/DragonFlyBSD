PROG=	timedwait_unlocked
SRCS=	timedwait_unlocked.c common.c
NO_MAN=
LDADD=  -lpthread

CFLAGS= -I ${.CURDIR}/../common
WARNS?=	3

.include <bsd.prog.mk>
.PATH: ${.CURDIR}/../common
