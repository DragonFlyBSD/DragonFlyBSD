.include "../../Makefile.inc1"
.include "../Makefile.block2"

.PATH: ${SRCDIR}/binutils ${SRCDIR}/binutils/doc

PROG=	readelf
SRCS=	readelf.c version.c unwind-ia64.c dwarf.c elfcomm.c
MFILE=	readelf${MANPAGEVER}.1
MAN=	${MFILE}
LDADD=	${LDADD_IBERTY} -lz

CFLAGS+=	-I${.OBJDIR}

${MFILE}: ${SRCDIR}/binutils/doc/readelf.1
	cp ${.ALLSRC} ${.TARGET}

dwarf.c: bfd_stdint.h

CLEANFILES+=	${MFILE} bfd_stdint.h

.if defined(IS_PRIMARY)
MLINKS+=	${MFILE} readelf.1
.endif

.include <bsd.prog.mk>
