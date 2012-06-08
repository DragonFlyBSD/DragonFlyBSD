PROG=	mount_tmpfs
SRCS=	mount_tmpfs.c fscopy.c
MAN=	mount_tmpfs.8
LDADD+=	-lutil
DPADD+=	${LIBUTIL}

NEWFS=	${.CURDIR}/../newfs
CFLAGS+=-I${.CURDIR}/../../sys -I${NEWFS}
.PATH:	${NEWFS}

.include <bsd.prog.mk>
