PROG=	mount_psshfs
SRCS=	psshfs.c fs.c node.c psbuf.c subr.c
MAN=	mount_psshfs.8

LDADD+= -lpuffs -lutil
DPADD+= ${LIBPUFFS} ${LIBUTIL}

.include <bsd.prog.mk>
