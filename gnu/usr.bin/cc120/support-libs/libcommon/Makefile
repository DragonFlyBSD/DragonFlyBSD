.include "../Makefile.inc"

LIB=		common
INTERNALLIB=	YES

# verbatim from Makefile
OBJS-libcommon= diagnostic-spec.o diagnostic.o diagnostic-color.o \
        diagnostic-show-locus.o diagnostic-format-json.o json.o \
        edit-context.o \
        pretty-print.o intl.o \
        sbitmap.o \
        vec.o input.o hash-table.o ggc-none.o memory-block.o \
        selftest.o selftest-diagnostic.o sort.o

.if ${CFLAGS:M-DTOOLCHAIN_CHECKING}
OBJS-libcommon+=	selftest.o selftest-diagnostic.o
.endif

SRCS+=		memory-block.cc sort.cc diagnostic-format-json.cc json.cc
OBJS+=		${OBJS-libcommon}
CFLAGS+=	-I${GCCDIR}/libbacktrace

# for pretty-print.c
.if ${CCVER:Mgcc120}
CXXFLAGS+=	-Wno-narrowing
.elif ${CCVER:Mclang*}
CXXFLAGS+=	-Wno-c++11-narrowing
.endif

.include <bsd.lib.mk>
