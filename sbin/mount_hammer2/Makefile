PROG=	mount_hammer2
SRCS=	mount_hammer2.c
MAN=

CFLAGS+= -I${.CURDIR}/..
CFLAGS+= -I${.CURDIR}/../../crypto/libressl/include
LDADD=  -lutil
DPADD=  ${LIBUTIL}

.include <bsd.prog.mk>
