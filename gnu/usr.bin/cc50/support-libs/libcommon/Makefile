.include "../Makefile.inc"

LIB=		common
INTERNALLIB=	YES

# verbatim from Makefile
OBJS-libcommon= diagnostic.o diagnostic-color.o pretty-print.o intl.o \
	vec.o  input.o version.o

OBJS=		${OBJS-libcommon}
CFLAGS+=	-I${GCCDIR}/libbacktrace

.for ofile in ${OBJS-libcommon}
${ofile}: ${ofile:.o=.c}
	${CXX} ${STATIC_CXXFLAGS} ${CXXFLAGS} -c ${.IMPSRC} -o ${.TARGET}
.endfor

.include <bsd.lib.mk>
