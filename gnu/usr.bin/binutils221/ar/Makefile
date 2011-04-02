.include "../Makefile.bu"

.PATH: ${SRCDIR}/binutils ${SRCDIR}/binutils/doc

PROG=	ar
SRCS=	ar.c not-ranlib.c
MAN=	ar221.1
MLINKS+=ar221.1 ar.1
NOSHARED?= yes

ar221.1: ${SRCDIR}/binutils/doc/ar.1
	cp ${.ALLSRC} ${.TARGET}

CLEANFILES+=	ar221.1

.include <bsd.prog.mk>
