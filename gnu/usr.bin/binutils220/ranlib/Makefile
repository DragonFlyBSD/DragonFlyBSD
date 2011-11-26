.include "../Makefile.bu"

.PATH: ${SRCDIR}/binutils ${SRCDIR}/binutils/doc

PROG=	ranlib
SRCS=	ar.c is-ranlib.c
MAN=	ranlib220.1

NOSHARED?= yes

ranlib220.1: ${SRCDIR}/binutils/doc/ranlib.1
	cp ${.ALLSRC} ${.TARGET}

CLEANFILES+=	ranlib220.1

.include <bsd.prog.mk>
