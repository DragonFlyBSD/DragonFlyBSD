CONTRIBDIR= ${.CURDIR}/../../contrib/byacc
.PATH: ${CONTRIBDIR}

PROG=	yacc
SRCS=	closure.c error.c graph.c lalr.c lr0.c main.c mkpar.c output.c \
	reader.c skeleton.c symtab.c verbose.c warshall.c

VER!=	cat ${CONTRIBDIR}/VERSION
CFLAGS+=-DYYPATCH=${VER}
# config.h defines
CFLAGS+=-DHAVE_FCNTL_H -DHAVE_MKSTEMP -DMIXEDCASE_FILENAMES

LINKS=	${BINDIR}/yacc ${BINDIR}/byacc
MLINKS=	yacc.1 byacc.1

.include <bsd.prog.mk>
