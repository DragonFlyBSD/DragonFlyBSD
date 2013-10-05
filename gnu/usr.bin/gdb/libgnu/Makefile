.include "../Makefile.inc"

LIB=	gnu
INTERNALLIB=	YES

SRCS=	fnmatch.c localcharset.c memmem.c strnlen1.c wctype-h.c

.PATH:	${BASEDIR}/gdb/gnulib/import

CFLAGS+=	-I${.CURDIR} -I${.OBJDIR} -I${BASEDIR}/gdb/gnulib/import

.include <bsd.lib.mk>
