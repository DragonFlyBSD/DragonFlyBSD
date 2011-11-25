.include "../Makefile.bu"

.PATH: ${SRCDIR}/binutils ${SRCDIR}/binutils/doc

PROG=	ranlib
SRCS=	ar.c is-ranlib.c
MAN=	ranlib222.1
MLINKS+=ranlib222.1 ranlib.1

NOSHARED?= yes

ranlib222.1: ${SRCDIR}/binutils/doc/ranlib.1
	cp ${.ALLSRC} ${.TARGET}

CLEANFILES+=	ranlib222.1

.include <bsd.prog.mk>
