PROG=	mount_fusefs
SRCS=	mount_fusefs.c

CFLAGS+= -I${.CURDIR} -I${.CURDIR}/../../sys

MAN=	mount_fusefs.8

DPADD=	${LIBUTIL}
LDADD=	-lutil

.include <bsd.prog.mk>
