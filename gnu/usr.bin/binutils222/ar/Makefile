.include "../Makefile.bu"

.PATH: ${SRCDIR}/binutils ${SRCDIR}/binutils/doc

PROG=	ar
SRCS=	ar.c not-ranlib.c
MAN=	ar222.1
MLINKS+=ar222.1 ar.1
NOSHARED?= yes

ar222.1: ${SRCDIR}/binutils/doc/ar.1
	cp ${.ALLSRC} ${.TARGET}

CLEANFILES+=	ar222.1

.include <bsd.prog.mk>
