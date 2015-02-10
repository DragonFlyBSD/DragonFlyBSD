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

GUTS=		../../guts/guts-common/libguts-common.na \
		../../guts/guts-target/libguts-target.na
LIBS=		${LIBCOMMONTARG} ${LIBCOMMON} ${LIBCPP} \
		${LIBBACKTRACE} ${LIBIBERTY} ${LIBDECNUMBER}
LDADD=		${LIBS}
DPADD=		${LIBS}


# hack to force c++ compiler to compile *.c files to create library
.for ofile in ${OBJS}
${ofile}: ${ofile:.o=.c}
	${CXX} ${STATIC_CXXFLAGS} ${CXXFLAGS} -c ${.IMPSRC} -o ${.TARGET}
.endfor

.include <bsd.prog.mk>
