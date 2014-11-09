CONTRIBDIR= ${.CURDIR}/../../contrib/byacc
.PATH: ${CONTRIBDIR}

PROG=	yacc
SRCS=	closure.c error.c graph.c lalr.c lr0.c main.c mkpar.c mstring.c \
	output.c reader.c symtab.c verbose.c warshall.c yaccpar.c

VER!=	cat ${CONTRIBDIR}/VERSION
CFLAGS+=-DYYPATCH=${VER}
# config.h defines
CFLAGS+=-DHAVE_FCNTL_H -DHAVE_MKSTEMP -DHAVE_VSNPRINTF -DMAXTABLE=65000
CFLAGS+=-DMIXEDCASE_FILENAMES

LINKS=	${BINDIR}/yacc ${BINDIR}/byacc
MLINKS=	yacc.1 byacc.1

.include <bsd.prog.mk>
