#
#
PROG=           nvmectl
SRCS=		nvmectl.c subs.c
MAN=            nvmectl.8

CFLAGS+=	-I${.CURDIR}/../../sys
LDADD=		-lutil
DPADD=		${LIBUTIL}

.include <bsd.prog.mk>
