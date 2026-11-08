GCC_LANG_DIR=	gcc/lto
TOP_PREFIX=	../../

.include "../../../Makefile.inc"

PROG_CXX=	lto1
NOMAN=		yes

# The LTO-specific object files inclued in $(LTO_EXE).
# verbatim from gcc/lto/Make-lang.in
LTO_OBJS = lto/lto-lang.o lto/lto.o lto/lto-object.o attribs.o lto/lto-partition.o lto/lto-symtab.o lto/lto-common.o

SRCS=		${LTO_OBJS:T:.o=.cc}

BACKEND=	${LIBBACKEND} ${LIBCOMMONTARG} ${LIBCOMMON} \
		${LIBCPP} ${LIBDECNUMBER}
LIBS=		${LIBCOMMON} ${LIBCPP} ${LIBBACKTRACE} ${LIBIBERTY} \
		${LIBDECNUMBER}
LDADD=		${BACKEND} ${BACKENDLIBS} ${LIBS}
DPADD=		${BACKEND} ${BACKENDLIBS} ${LIBS}

.include <bsd.prog.mk>
