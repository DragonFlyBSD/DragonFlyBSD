PROG=	hammer2
SRCS=	main.c subs.c icrc.c crypto.c
SRCS+=	cmd_remote.c cmd_snapshot.c cmd_pfs.c
SRCS+=	cmd_service.c cmd_leaf.c cmd_debug.c
SRCS+=	cmd_rsa.c cmd_stat.c
SRCS+=	msg.c msg_lnk.c
#MAN=	hammer2.8
NOMAN=	TRUE
DEBUG_FLAGS=-g

CFLAGS+= -I${.CURDIR}/../../sys
CFLAGS+= -pthread
LDADD=	-lm -lutil -lmd -lcrypto
DPADD=	${LIBM} ${LIBUTIL} ${LIBMD} ${LIBCRYPTO}

#.PATH: ${.CURDIR}/../../sys/libkern
#SRCS+= crc32.c

.include <bsd.prog.mk>
