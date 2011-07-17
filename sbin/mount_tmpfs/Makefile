PROG=	mount_tmpfs
SRCS=	mount_tmpfs.c fscopy.c getmntopts.c
MAN=	mount_tmpfs.8
LDADD+=	-lutil
DPADD+=	${LIBUTIL}

MOUNT=	${.CURDIR}/../mount
CFLAGS+=-I${.CURDIR}/../../sys -I${MOUNT}
.PATH:	${MOUNT}

NEWFS=	${.CURDIR}/../newfs
CFLAGS+=-I${NEWFS}
.PATH:	${NEWFS}

.include <bsd.prog.mk>
