.include "../Makefile.inc"

LIB=		txi
INTERNALLIB= 	true

CONTRIBDIR=	${BASEDIR}/lib
.PATH:		${CONTRIBDIR}

CFLAGS+=	-I${CONTRIBDIR} -I${.CURDIR} -I${.OBJDIR}
CFLAGS+=	-I${BASEDIR} -I${.CURDIR}/../libgnu
CFLAGS+=	-I${BASEDIR}/gnulib/lib
CFLAGS+=	-DHAVE_CONFIG_H

SRCS=		substring.c \
		xexit.c

.include <bsd.lib.mk>
