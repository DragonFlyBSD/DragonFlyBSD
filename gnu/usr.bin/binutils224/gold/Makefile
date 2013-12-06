.include "../Makefile.inc0"

.PATH: ${SRCDIR}/gold

PROG_CXX= ld.gold
NOMAN=
SRCS+=	main.cc			\
	i386.cc			\
	x86_64.cc		\
	sparc.cc		\
	powerpc.cc		\
	arm.cc			\
	arm-reloc-property.cc	\
	tilegx.cc

# standalone programs:
#	incremental-dump.cc
#	dwp.cc

WARNS=	1
CFLAGS=	 -DHAVE_CONFIG_H
CFLAGS+= -DLARGEFILE_SOURCE
CFLAGS+= -D_FILE_OFFSET_BITS=64
CFLAGS+= -DGOLD_DEFAULT_MACHINE=${GOLD_DEFAULT_MACHINE}
CFLAGS+= -DGOLD_DEFAULT_SIZE=${GOLD_DEFAULT_SIZE}
CFLAGS+= -DBINDIR="\"${TOOLS_PREFIX}${BINDIR}\""
CFLAGS+= -DTOOLBINDIR="\"${TOOLS_PREFIX}${BINDIR}\""
CFLAGS+= -DTOOLLIBDIR="\"${TOOLS_PREFIX}${LIBDIR}\""
CFLAGS+= -I. -I${.CURDIR}
CFLAGS+= -I${SRCDIR}/include -I${SRCDIR}/elfcpp
CFLAGS+= -I${.CURDIR}/../libgold

DPADD= ${RELTOP}/libgold/libgold.a
LDADD= ${RELTOP}/libgold/libgold.a
DPADD+=	${RELTOP}/libiberty/libiberty.a
LDADD+=	${RELTOP}/libiberty/libiberty.a
DPADD+=	${LIBZ}
LDADD+=	-lz


.include <bsd.prog.mk>
