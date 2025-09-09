# If we defined SRCS, the mkdep fails because it evaluates files with .c
# extension with cc instead of c++ (go figure).  To avoid that, we need
# to define OBJS directly

TOP_PREFIX=	../../

.include "../../../Makefile.inc"

PROG_CXX=	lto-wrapper
NOMAN=		yes

SRCS=
OBJS=		lto-wrapper.o \
		collect-utils.o \
		ggc-none.o

GUTS=		../../guts/guts-common/libguts-common.a \
		../../guts/guts-target/libguts-target.a
LIBS=		${LIBCOMMONTARG} ${LIBCOMMON} ${LIBCPP} \
		${LIBBACKTRACE} ${LIBIBERTY} ${LIBDECNUMBER}
LDADD=		${LIBS}
DPADD=		${LIBS}

.include <bsd.prog.mk>
