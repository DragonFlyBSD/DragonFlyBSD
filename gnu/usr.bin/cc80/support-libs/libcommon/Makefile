.include "../Makefile.inc"

LIB=		common
INTERNALLIB=	YES

# verbatim from Makefile
OBJS-libcommon= diagnostic.o diagnostic-color.o diagnostic-show-locus.o \
	edit-context.o pretty-print.o intl.o sbitmap.o \
	vec.o input.o version.o hash-table.o ggc-none.o

.if ${CFLAGS:M-DTOOLCHAIN_CHECKING}
OBJS-libcommon+=	selftest.o selftest-diagnostic.o
.endif

SRCS+=		memory-block.cc
OBJS+=		${OBJS-libcommon}
CFLAGS+=	-I${GCCDIR}/libbacktrace

# for pretty-print.c
.if ${CCVER:Mgcc80}
CXXFLAGS+=	-Wno-narrowing
.endif

.for ofile in ${OBJS-libcommon}
${ofile}: ${ofile:.o=.c}
	${CXX} ${STATIC_CXXFLAGS} ${CXXFLAGS} -c ${.IMPSRC} -o ${.TARGET}
.endfor

.include <bsd.lib.mk>
