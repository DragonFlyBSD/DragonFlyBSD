.include "../Makefile.inc1"
.include "../Makefile.bu"

.PATH: ${SRCDIR}/binutils ${SRCDIR}/binutils/doc

PROG=	strings
MFILE=	strings${MANPAGEVER}.1
MAN=	${MFILE}
WARNS=	2

${MFILE}: ${SRCDIR}/binutils/doc/strings.1
	cp ${.ALLSRC} ${.TARGET}

CLEANFILES+=	${MFILE}

.if defined(IS_PRIMARY)
MLINKS+=	${MFILE} strings.1
.endif

.include <bsd.prog.mk>
