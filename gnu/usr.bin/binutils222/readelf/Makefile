.include "../Makefile.bu"

.PATH: ${SRCDIR}/binutils ${SRCDIR}/binutils/doc

PROG=	readelf
SRCS=	readelf.c unwind-ia64.c dwarf.c elfcomm.c
MAN=	readelf222.1
MLINKS+=readelf222.1 readelf.1
WARNS=	1

readelf222.1: ${SRCDIR}/binutils/doc/readelf.1
	cp ${.ALLSRC} ${.TARGET}

CLEANFILES+=	readelf222.1

.include <bsd.prog.mk>
