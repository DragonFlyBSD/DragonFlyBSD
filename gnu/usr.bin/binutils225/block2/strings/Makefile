.include "../../Makefile.inc1"
.include "../Makefile.block2"

.PATH: ${SRCDIR}/binutils ${SRCDIR}/binutils/doc

PROG=	strings
MFILE=	strings${MANPAGEVER}.1
MAN=	${MFILE}
LDADD=	${STD_LDADD} -lz

${MFILE}: ${SRCDIR}/binutils/doc/strings.1
	cp ${.ALLSRC} ${.TARGET}

CLEANFILES+=	${MFILE}

.if defined(IS_PRIMARY)
MLINKS+=	${MFILE} strings.1
.endif

.include <bsd.prog.mk>
