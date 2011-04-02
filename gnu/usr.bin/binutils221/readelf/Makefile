.include "../Makefile.bu"

.PATH: ${SRCDIR}/binutils ${SRCDIR}/binutils/doc

PROG=	readelf
SRCS=	readelf.c unwind-ia64.c dwarf.c elfcomm.c
MAN=	readelf221.1
MLINKS+=readelf221.1 readelf.1
WARNS=	1

readelf221.1: ${SRCDIR}/binutils/doc/readelf.1
	cp ${.ALLSRC} ${.TARGET}

CLEANFILES+=	readelf221.1

.include <bsd.prog.mk>
