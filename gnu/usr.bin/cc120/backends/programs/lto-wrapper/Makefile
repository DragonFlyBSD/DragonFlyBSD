TOP_PREFIX=	../../

.include "../../../Makefile.inc"

PROG_CXX=	lto-wrapper
NOMAN=		yes
WARNS=		1

# verbatim from gcc/Makefile.in
LTO_WRAPPER_OBJS = lto-wrapper.o collect-utils.o ggc-none.o

SRCS=		${LTO_WRAPPER_OBJS:.o=.cc}

GUTS=		../../guts/guts-common/libguts-common.a \
		../../guts/guts-target/libguts-target.a
LIBS=		${LIBCOMMONTARG} ${LIBCOMMON} ${LIBCPP} \
		${LIBBACKTRACE} ${LIBIBERTY} ${LIBDECNUMBER}
LDADD=		${LIBS}
DPADD=		${LIBS}

.include <bsd.prog.mk>
