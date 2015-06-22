PROG=	hammer2
SRCS=	main.c subs.c icrc.c
SRCS+=	cmd_remote.c cmd_snapshot.c cmd_pfs.c
SRCS+=	cmd_service.c cmd_leaf.c cmd_debug.c
SRCS+=	cmd_rsa.c cmd_stat.c cmd_setcomp.c cmd_setcheck.c
SRCS+=	cmd_bulkfree.c
SRCS+=	print_inode.c
MAN=	hammer2.8
#NOMAN=	TRUE
DEBUG_FLAGS=-g

CFLAGS+= -I${.CURDIR}/../../sys
CFLAGS+= -pthread
LDADD=	-ldmsg -lm -lutil -lmd -lcrypto
DPADD=	${LIBDMSG} ${LIBM} ${LIBUTIL} ${LIBMD} ${LIBCRYPTO}

#.PATH: ${.CURDIR}/../../sys/libkern
#SRCS+= crc32.c

.include <bsd.prog.mk>
