.include "../Makefile.inc1"
.include "../Makefile.bu"

.PATH: ${SRCDIR}/binutils ${SRCDIR}/binutils/doc

PROG=	objcopy
SRCS=	not-strip.c objcopy.c
MFILE=	objcopy${MANPAGEVER}.1
MAN=	${MFILE}
WARNS=	1

${MFILE}: ${SRCDIR}/binutils/doc/objcopy.1
	cp ${.ALLSRC} ${.TARGET}

CLEANFILES+=	${MFILE}

.if defined(IS_PRIMARY)
MLINKS+=	${MFILE} objcopy.1
.endif

.include <bsd.prog.mk>
