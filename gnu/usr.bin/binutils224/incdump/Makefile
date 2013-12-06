.include "../Makefile.gld"

PROG_CXX= incremental-dump
SRCS+=	incremental-dump.cc	\
	i386.cc			\
	x86_64.cc		\
	sparc.cc		\
	powerpc.cc		\
	arm.cc			\
	arm-reloc-property.cc	\
	tilegx.cc
NOMAN=
WARNS=	1
CFLAGS+= -I${.CURDIR}/../libgold

DPADD= ${RELTOP}/libgold/libgold.a
LDADD= ${RELTOP}/libgold/libgold.a
DPADD+=	${RELTOP}/libiberty/libiberty.a
LDADD+=	${RELTOP}/libiberty/libiberty.a
DPADD+=	${LIBZ}
LDADD+=	-lz

.include <bsd.prog.mk>
