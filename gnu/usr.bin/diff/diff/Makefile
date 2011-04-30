.include "../Makefile.inc0"
.PATH:	${SRCDIR}/src ${SRCDIR}/man

PROG=		diff
CFLAGS+=	-I${SRCDIR}/lib -I${.CURDIR}/../libdiffutils

SRCS=		analyze.c \
		context.c \
		diff.c \
		dir.c \
		ed.c \
		ifdef.c \
		io.c \
		normal.c \
		side.c \
		util.c

.include <bsd.prog.mk>
