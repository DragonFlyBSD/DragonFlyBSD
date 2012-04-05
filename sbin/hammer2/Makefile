PROG=	hammer2
SRCS=	main.c subs.c
SRCS+=	cmd_remote.c cmd_snapshot.c
#MAN=	hammer2.8
NOMAN=	TRUE

CFLAGS+= -I${.CURDIR}/../../sys
LDADD=	-lm -lutil -lmd
DPADD=	${LIBM} ${LIBUTIL} ${LIBMD}

#.PATH: ${.CURDIR}/../../sys/libkern
#SRCS+= crc32.c

.include <bsd.prog.mk>
