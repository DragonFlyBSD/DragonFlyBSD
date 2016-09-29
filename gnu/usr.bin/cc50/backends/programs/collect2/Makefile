# If we defined SRCS, the mkdep fails because it evaluates files with .c
# extension with cc instead of c++ (go figure).  To avoid that, we need
# to define OBJS directly

GCC_LANG_DIR=	gcc
TOP_PREFIX=	../../

.include "../../../Makefile.inc"
.include "../../../Makefile.langs"

PROG_CXX=	collect2
NOMAN=		yes

###CFLAGS+=	-static-libstdc++

# verbatim from gcc/lto/Make-lang.in
COLLECT_OBJS= 	collect2.o collect2-aix.o tlink.o vec.o ggc-none.o \
		collect-utils.o file-find.o hash-table.o
OBJS=		${COLLECT_OBJS:T}
SRCS=

LIBS=		${LIBCOMMON} ${LIBCPP} ${LIBBACKTRACE} ${LIBIBERTY} \
		${LIBDECNUMBER}
LDADD=		${LIBS}
DPADD=		${LIBS}


# hack to force c++ compiler to compile *.c files to create library
.for ofile in ${OBJS}
${ofile}: ${ofile:.o=.c}
	${CXX} ${STATIC_CXXFLAGS} ${CXXFLAGS} -c ${.IMPSRC} -o ${.TARGET}
.endfor

.include <bsd.prog.mk>
