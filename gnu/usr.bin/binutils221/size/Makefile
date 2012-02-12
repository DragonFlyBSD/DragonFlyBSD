.include "../Makefile.inc1"
.include "../Makefile.bu"

.PATH: ${SRCDIR}/binutils ${SRCDIR}/binutils/doc

PROG=	size
MFILE=	size${MANPAGEVER}.1
MAN=	${MFILE}
WARNS=	2

${MFILE}: ${SRCDIR}/binutils/doc/size.1
	cp ${.ALLSRC} ${.TARGET}

CLEANFILES+=	${MFILE}

.if defined(IS_PRIMARY)
MLINKS+=	${MFILE} size.1
.endif

.include <bsd.prog.mk>
