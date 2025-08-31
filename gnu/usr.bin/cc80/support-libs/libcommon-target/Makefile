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

OBJS=		${OBJS-libcommon-target}
CFLAGS+=	-DPREFIX=\"${TOOLS_PREFIX}/usr\"

# for opts-common.c
.if (${CCVER:Mgcc80} || ${CCVER:Mgcc1[0-9]0})
CXXFLAGS+=	-Wno-narrowing
.elif ${CCVER:Mclang*}
CXXFLAGS+=	-Wno-c++11-narrowing
.endif

.for ofile in ${OBJS-libcommon-target}
${ofile}: ${ofile:.o=.c}
	${CXX} ${STATIC_CXXFLAGS} ${CXXFLAGS} -c ${.IMPSRC} -o ${.TARGET}
.endfor

.include <bsd.lib.mk>
