.include "../Makefile.inc1"
.include "../Makefile.bu"

.PATH: ${SRCDIR}/binutils ${SRCDIR}/binutils/doc

PROG=	readelf
SRCS=	readelf.c unwind-ia64.c dwarf.c elfcomm.c
MFILE=	readelf${MANPAGEVER}.1
MAN=	${MFILE}
WARNS=	1

${MFILE}: ${SRCDIR}/binutils/doc/readelf.1
	cp ${.ALLSRC} ${.TARGET}

CLEANFILES+=	${MFILE}

.if defined(IS_PRIMARY)
MLINKS+=	${MFILE} readelf.1
.endif

.include <bsd.prog.mk>
