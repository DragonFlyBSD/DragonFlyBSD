TOP_PREFIX=	../../

.include "../../../Makefile.inc"

PROG_CXX=	collect2
NOMAN=		yes
WARNS=		1

## LDFLAGS+=	-static-libstdc++

# from gcc/Makefile.in
# - excluded 'collect-aix.o' (effective when CROSS_AIX_SUPPORT)
# - excluded 'selftest.o' (effective when CHECKING_P)
COLLECT2_OBJS = collect2.o vec.o ggc-none.o \
  collect-utils.o file-find.o hash-table.o

SRCS=		${COLLECT2_OBJS:T:.o=.cc}
LIBS=		${LIBCOMMON} ${LIBCPP} ${LIBBACKTRACE} ${LIBIBERTY} \
		${LIBDECNUMBER}
LDADD=		${LIBS}
DPADD=		${LIBS}

.include <bsd.prog.mk>
