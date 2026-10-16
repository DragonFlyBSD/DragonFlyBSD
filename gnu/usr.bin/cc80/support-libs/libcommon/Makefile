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

SRCS=		${OBJS-libcommon:.o=.c}
SRCS+=		memory-block.cc

CFLAGS+=	-I${GCCDIR}/libbacktrace

# for pretty-print.c
.if ${CCVER:Mgcc*} && ${CCVER:S/gcc//} >= 80
CXXFLAGS+=	-Wno-narrowing
.elif ${CCVER:Mclang*}
CXXFLAGS+=	-Wno-c++11-narrowing
.endif

.include <bsd.lib.mk>

# hack to force c++ compiler to compile *.c files
MKDEPCC:=	${CXX}
CC:=		${CXX}
CFLAGS:=	${CXXFLAGS}
