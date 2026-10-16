.include "../Makefile.inc"

LIB=		common-target
INTERNALLIB=	YES

.PATH: $(srcdir)/common
.PATH: $(srcdir)/common/config/i386

# Almost verbatim from Makefile
OBJS-libcommon-target = $(common_out_object_file) prefix.o params.o \
	opts.o opts-common.o options.o vec.o hooks.o common-targhooks.o \
	hash-table.o file-find.o spellcheck.o

common_out_object_file=	i386-common.o

.if ${CFLAGS:M-DTOOLCHAIN_CHECKING}
OBJS-libcommon-target+=	selftest.o
.endif

SRCS=		${OBJS-libcommon-target:.o=.c}

CFLAGS+=	-DPREFIX=\"${TOOLS_PREFIX}/usr\"

# for opts-common.c
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
