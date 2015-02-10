.include "../Makefile.gld"

PROG_CXX= ld.gold
SRCS+=	main.cc			\
	i386.cc			\
	x86_64.cc		\
	sparc.cc		\
	powerpc.cc		\
	arm.cc			\
	arm-reloc-property.cc
NOMAN=
WARNS=	1
CFLAGS+= -I${.CURDIR}/../libgold

DPADD= ${RELTOP}/libgold/libgold.a
LDADD= ${RELTOP}/libgold/libgold.a
DPADD+=	${RELTOP}/libiberty/libiberty.a
LDADD+=	${RELTOP}/libiberty/libiberty.a
DPADD+=	${LIBZ}
LDADD+=	-lz

# link-time optimization requires dlopen() which means ld cannot be built
# statically and also support -plugin, not even for cross-tools.
.undef NOSHARED

.include <bsd.prog.mk>
