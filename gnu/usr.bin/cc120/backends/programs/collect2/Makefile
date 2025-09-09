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

# verbatim from gcc/Makefile.in
COLLECT_OBJS=	collect2.o vec.o ggc-none.o \
		collect-utils.o file-find.o hash-table.o
# only when CROSS_AIX_SUPPORT
#COLLECT_OBJS+=	collect2-aix.o

OBJS=		${COLLECT_OBJS:T}
SRCS=
LIBS=		${LIBCOMMON} ${LIBCPP} ${LIBBACKTRACE} ${LIBIBERTY} \
		${LIBDECNUMBER}
LDADD=		${LIBS}
DPADD=		${LIBS}

.include <bsd.prog.mk>
