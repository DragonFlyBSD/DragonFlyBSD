.include "../../Makefile.inc1"
.include "../Makefile.block2"

.PATH: ${SRCDIR}/binutils ${SRCDIR}/binutils/doc

PROG=	addr2line
SRCS=	addr2line.c
MFILE=	addr2line${MANPAGEVER}.1
MAN=	${MFILE}
LDADD=	${STD_LDADD} -lz

${MFILE}: ${SRCDIR}/binutils/doc/addr2line.1
	cp ${.ALLSRC} ${.TARGET}

CLEANFILES+=	${MFILE}

.if defined(IS_PRIMARY)
MLINKS+=	${MFILE} addr2line.1
.endif

.include <bsd.prog.mk>
