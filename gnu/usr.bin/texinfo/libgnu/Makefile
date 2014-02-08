.include "../Makefile.inc"

LIB=		gnu
INTERNALLIB=	true

CONTRIBDIR=	${BASEDIR}/gnulib/lib
.PATH:		${CONTRIBDIR} ${CONTRIBDIR}/uniwidth

CFLAGS+=	-I${CONTRIBDIR} -I${.CURDIR} -I${.OBJDIR}
CFLAGS+=	-DHAVE_CONFIG_H

SRCS=		localcharset.c \
		malloca.c \
		mbchar.c \
		mbscasecmp.c \
		mbschr.c \
		mbslen.c \
		mbsncasecmp.c \
		mbsstr.c \
		mbswidth.c \
		strnlen1.c \
		width.c \
		xalloc-die.c \
		xsetenv.c \
		argz.c \
		error.c \
		exitfail.c \
		getopt.c \
		getopt1.c \
		memmem.c \
		mempcpy.c \
		tempname.c \
		wcwidth.c \
		xmalloc.c	

.include <bsd.lib.mk>
