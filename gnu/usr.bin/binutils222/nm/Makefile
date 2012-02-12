.include "../Makefile.inc1"
.include "../Makefile.bu"

.PATH: ${SRCDIR}/binutils ${SRCDIR}/binutils/doc

PROG=	nm
MFILE=	nm${MANPAGEVER}.1
MAN=	${MFILE}
WARNS=	2

${MFILE}: ${SRCDIR}/binutils/doc/nm.1
	cp ${.ALLSRC} ${.TARGET}

CLEANFILES+=	${MFILE}

.if defined(IS_PRIMARY)
MLINKS+=	${MFILE} nm.1
.endif

.include <bsd.prog.mk>
