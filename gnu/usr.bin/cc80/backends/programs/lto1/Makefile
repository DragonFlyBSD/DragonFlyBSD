GCC_LANG_DIR=	gcc/lto
TOP_PREFIX=	../../

.include "../../../Makefile.inc"
.include "../../../Makefile.langs"

PROG_CXX=	lto1
NOMAN=		yes

# verbatim from gcc/lto/Make-lang.in
LTO_OBJS=	lto/lto-lang.o lto/lto.o lto/lto-object.o attribs.o \
		lto/lto-partition.o lto/lto-symtab.o

SRCS=		${LTO_OBJS:T:.o=.c} main.c

BACKEND=	${LIBBACKEND} ${LIBCOMMONTARG} ${LIBCOMMON} \
		${LIBCPP} ${LIBDECNUMBER}
LIBS=		${LIBCOMMON} ${LIBCPP} ${LIBBACKTRACE} ${LIBIBERTY} \
		${LIBDECNUMBER}
LDADD=		${BACKEND} ${BACKENDLIBS} ${LIBS}
DPADD=		${BACKEND} ${BACKENDLIBS} ${LIBS}

.include <bsd.prog.mk>

# hack to force c++ compiler to compile *.c files
MKDEPCC:=	${CXX}
CC:=		${CXX}
CFLAGS:=	${CXXFLAGS}
