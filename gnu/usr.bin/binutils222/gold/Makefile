.include "../Makefile.gld"

PROG_CXX= ld.gold
SRCS+=	  main.cc
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
