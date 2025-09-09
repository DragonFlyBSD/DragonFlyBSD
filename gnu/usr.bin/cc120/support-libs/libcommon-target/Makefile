.include "../Makefile.inc"

LIB=		common-target
INTERNALLIB=	YES

.PATH: $(srcdir)/common
.PATH: $(srcdir)/common/config/i386

# Almost verbatim from Makefile
OBJS-libcommon-target = ${common_out_object_file} prefix.o \
	opts.o opts-common.o options.o vec.o hooks.o common-targhooks.o \
	hash-table.o file-find.o spellcheck.o opt-suggestions.o

common_out_object_file=	i386-common.o

.if ${CFLAGS:M-DTOOLCHAIN_CHECKING}
OBJS-libcommon-target+=	selftest.o
.endif

OBJS=		${OBJS-libcommon-target}
CFLAGS+=	-DPREFIX=\"${TOOLS_PREFIX}/usr\"

# for opts-common.c
.if ${CCVER:Mgcc80} || ${CCVER:Mgcc120}
CXXFLAGS+=	-Wno-narrowing
.elif ${CCVER:Mclang*}
CXXFLAGS+=	-Wno-c++11-narrowing
.endif

.include <bsd.lib.mk>
