PROG=	mount_psshfs
SRCS=	psshfs.c fs.c node.c psbuf.c subr.c getmntopts.c
LDADD+= -lpuffs -lutil
DPADD+= ${LIBPUFFS} ${LIBUTIL}

MAN=	mount_psshfs.8

.include <bsd.prog.mk>
