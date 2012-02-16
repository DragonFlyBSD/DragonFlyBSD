.include "../Makefile.inc1"
.include "../Makefile.bu"

.PATH: ${SRCDIR}/binutils

PROG=	c++filt
SRCS=   cxxfilt.c
MFILE=	c++filt${MANPAGEVER}.1
MAN=	${MFILE}
WARNS=	1

${MFILE}: ${.CURDIR}/c++filt.1
	cp ${.ALLSRC} ${.TARGET}

CLEANFILES+=	${MFILE}

.if defined(IS_PRIMARY)
MLINKS+=	${MFILE} c++filt.1
.endif

.include <bsd.prog.mk>
