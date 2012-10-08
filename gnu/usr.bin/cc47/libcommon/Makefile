.include "../Makefile.inc"

LIB=		common
INTERNALLIB=	YES

# verbatim from Makefile
OBJS-libcommon = diagnostic.o pretty-print.o intl.o input.o version.o

SRCS=	${OBJS-libcommon:.o=.c}

.include <bsd.lib.mk>
