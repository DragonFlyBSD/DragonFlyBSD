.include "../Makefile.bu"

.PATH: ${SRCDIR}/binutils ${SRCDIR}/binutils/doc

PROG=	ranlib
SRCS=	ar.c is-ranlib.c
MAN=	ranlib221.1
MLINKS+=ranlib221.1 ranlib.1

NOSHARED?= yes

ranlib221.1: ${SRCDIR}/binutils/doc/ranlib.1
	cp ${.ALLSRC} ${.TARGET}

CLEANFILES+=	ranlib221.1

.include <bsd.prog.mk>
