TOP_PREFIX=	../../

.include "../../../Makefile.inc"

PROG_CXX=	lto-wrapper
NOMAN=		yes

SRCS=		lto-wrapper.c \
		collect-utils.c \
		ggc-none.c

GUTS=		../../guts/guts-common/libguts-common.a \
		../../guts/guts-target/libguts-target.a
LIBS=		${LIBCOMMONTARG} ${LIBCOMMON} ${LIBCPP} \
		${LIBBACKTRACE} ${LIBIBERTY} ${LIBDECNUMBER}
LDADD=		${LIBS}
DPADD=		${LIBS}

.include <bsd.prog.mk>

# hack to force c++ compiler to compile *.c files
MKDEPCC:=	${CXX}
CC:=		${CXX}
CFLAGS:=	${CXXFLAGS}
