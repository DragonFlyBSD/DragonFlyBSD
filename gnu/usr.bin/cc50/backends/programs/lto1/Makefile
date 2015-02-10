# If we defined SRCS, the mkdep fails because it evaluates files with .c
# extension with cc instead of c++ (go figure).  To avoid that, we need
# to define OBJS directly

GCC_LANG_DIR=	gcc/lto
TOP_PREFIX=	../../

.include "../../../Makefile.inc"
.include "../../../Makefile.langs"

PROG_CXX=	lto1
NOMAN=		yes

# verbatim from gcc/lto/Make-lang.in
LTO_OBJS= 	lto/lto-lang.o lto/lto.o lto/lto-object.o attribs.o \
		lto/lto-partition.o lto/lto-symtab.o
OBJS=		${LTO_OBJS:T} main.o tree-browser.o
SRCS=		

BACKEND=	${LIBBACKEND} ${LIBCOMMONTARG} ${LIBCOMMON} \
		${LIBCPP} ${LIBDECNUMBER}
LIBS=		${LIBCOMMON} ${LIBCPP} ${LIBBACKTRACE} ${LIBIBERTY} \
		${LIBDECNUMBER}
LDADD=		${BACKEND} ${BACKENDLIBS} ${LIBS}
DPADD=		${BACKEND} ${BACKENDLIBS} ${LIBS}


# hack to force c++ compiler to compile *.c files to create library
.for ofile in ${OBJS}
${ofile}: ${ofile:.o=.c}
	${CXX} ${STATIC_CXXFLAGS} ${CXXFLAGS} -c ${.IMPSRC} -o ${.TARGET}
.endfor

.include <bsd.prog.mk>
