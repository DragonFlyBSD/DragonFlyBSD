.include "../Makefile.inc1"
.include "../Makefile.bu"

.PATH: ${SRCDIR}/binutils ${SRCDIR}/binutils/doc

PROG=	ar
SRCS=	ar.c not-ranlib.c
MFILE=	ar${MANPAGEVER}.1
MAN=	${MFILE}
NOSHARED?= yes

${MFILE}: ${SRCDIR}/binutils/doc/ar.1
	cp ${.ALLSRC} ${.TARGET}

CLEANFILES+=	${MFILE}

.if defined(IS_PRIMARY)
MLINKS+=	${MFILE} ar.1
.endif

.include <bsd.prog.mk>
