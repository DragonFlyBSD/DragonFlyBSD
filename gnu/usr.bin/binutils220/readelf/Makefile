.include "../Makefile.bu"

.PATH: ${SRCDIR}/binutils ${SRCDIR}/binutils/doc

PROG=	readelf
MAN=	readelf220.1
WARNS=	1

readelf220.1: ${SRCDIR}/binutils/doc/readelf.1
	cp ${.ALLSRC} ${.TARGET}

CLEANFILES+=	readelf220.1

.include <bsd.prog.mk>
