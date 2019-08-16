PROG=	mount_hammer2
MAN=	mount_hammer2.8

CFLAGS+= -I${.CURDIR}/../../sys
CFLAGS+= -I${.CURDIR}/../../crypto/libressl/include
LDADD=  -lutil
DPADD=  ${LIBUTIL}

.include <bsd.prog.mk>
