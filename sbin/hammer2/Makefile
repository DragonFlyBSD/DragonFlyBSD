PROG=	hammer2
SRCS=	main.c subs.c icrc.c msg.c
SRCS+=	cmd_remote.c cmd_snapshot.c cmd_pfs.c
SRCS+=	cmd_node.c cmd_leaf.c cmd_debug.c
#MAN=	hammer2.8
NOMAN=	TRUE

CFLAGS+= -I${.CURDIR}/../../sys
CFLAGS+= -pthread
LDADD=	-lm -lutil -lmd
DPADD=	${LIBM} ${LIBUTIL} ${LIBMD}

#.PATH: ${.CURDIR}/../../sys/libkern
#SRCS+= crc32.c

.include <bsd.prog.mk>
