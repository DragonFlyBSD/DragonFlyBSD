PROG=	sysvipcd
MAN=	sysvipcd.8

.PATH: ${.CURDIR}/../../lib/libc/sysvipc
SRCS=	sysvipcd.c perm.c shmd.c sockets.c sysvipc_hash.c lock.c utilsd.c

HEADERSDIR=${.CURDIR}/../../lib/libc/sysvipc
CFLAGS+=-I$(HEADERSDIR)

LDADD=	-lutil
DPADD=	${LIBUTIL}

.include <bsd.prog.mk>


