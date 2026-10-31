.include "../Makefile.inc"

LIB=		common
INTERNALLIB=	YES
WARNS=		1

# Verbatim from gcc/Makefile.in, except that 'selftest.o' and
# 'selftest-diagnostic.o' are moved under -DTOOLCHAIN_CHECKING.
OBJS-libcommon= diagnostic-spec.o diagnostic.o diagnostic-color.o \
        diagnostic-show-locus.o diagnostic-format-json.o json.o \
        edit-context.o \
        pretty-print.o intl.o \
        sbitmap.o \
        vec.o input.o hash-table.o ggc-none.o memory-block.o \
        sort.o

.if ${CFLAGS:M-DTOOLCHAIN_CHECKING}
OBJS-libcommon+=	selftest.o selftest-diagnostic.o
.endif

SRCS=		${OBJS-libcommon:T:.o=.cc}

CFLAGS+=	-I${GCCDIR}/libbacktrace

FLAGS_GROUPS=		warn
warn_FLAGS_FILES=	pretty-print.cc
.if ${CCVER:Mgcc*} && ${CCVER:S/gcc//} >= 80
warn_FLAGS=	-Wno-narrowing
.elif ${CCVER:Mclang*}
warn_FLAGS=	-Wno-c++11-narrowing
.endif

.include <bsd.lib.mk>
