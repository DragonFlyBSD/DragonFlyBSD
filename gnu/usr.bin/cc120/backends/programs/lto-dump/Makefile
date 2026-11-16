GCC_LANG_DIR=	gcc/lto
TOP_PREFIX=	../../

.include "../../../Makefile.inc"
.include "../../../../Makefile.cco"

PROG_CXX=	lto-dump
MFILE=		lto-dump${MANPAGEVER}.1
MAN=		${MFILE}

# The LTO-specific object files inclued in $(LTO_DUMP_EXE).
# verbatim from gcc/lto/Make-lang.in
LTO_DUMP_OBJS = lto/lto-lang.o lto/lto-object.o attribs.o lto/lto-partition.o lto/lto-symtab.o lto/lto-dump.o lto/lto-common.o

SRCS=		${LTO_DUMP_OBJS:T:.o=.cc}

BACKEND=	${LIBBACKEND} ${LIBCOMMONTARG} ${LIBCOMMON} \
		${LIBCPP} ${LIBDECNUMBER}
LIBS=		${LIBCOMMON} ${LIBCPP} ${LIBBACKTRACE} ${LIBIBERTY} \
		${LIBDECNUMBER}
LDADD=		${BACKEND} ${BACKENDLIBS} ${LIBS}
DPADD=		${BACKEND} ${BACKENDLIBS} ${LIBS}

${MFILE}: ${GCCDIR}/gcc/doc/lto-dump.1
	cp ${.ALLSRC} ${.TARGET}

CLEANFILES+=	${MFILE}

.if defined(IS_PRIMARY)
MLINKS+=	${MFILE} lto-dump.1
.endif

.include <bsd.prog.mk>
