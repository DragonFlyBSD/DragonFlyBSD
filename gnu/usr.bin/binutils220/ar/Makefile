.include "../Makefile.bu"

.PATH: ${SRCDIR}/binutils ${SRCDIR}/binutils/doc

PROG=	ar
SRCS=	ar.c not-ranlib.c
MAN=	ar220.1
NOSHARED?= yes

ar220.1: ${SRCDIR}/binutils/doc/ar.1
	cp ${.ALLSRC} ${.TARGET}

CLEANFILES+=	ar220.1

.include <bsd.prog.mk>
