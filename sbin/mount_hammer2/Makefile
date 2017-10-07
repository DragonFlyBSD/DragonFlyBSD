PROG=	mount_hammer2
SRCS=	mount_hammer2.c
MAN=	mount_hammer2.8

CFLAGS+= -I${.CURDIR}/..
CFLAGS+= -I${.CURDIR}/../../crypto/libressl/include
LDADD=  -lutil
DPADD=  ${LIBUTIL}

.include <bsd.prog.mk>
