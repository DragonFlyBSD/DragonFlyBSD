.include "../../Makefile.inc1"
.include "../Makefile.block2"

.PATH: ${SRCDIR}/binutils

PROG=	c++filt
SRCS=   cxxfilt.c
MFILE=	c++filt${MANPAGEVER}.1
MAN=	${MFILE}
LDADD=	${STD_LDADD} -lz

${MFILE}: ${.CURDIR}/c++filt.1
	cp ${.ALLSRC} ${.TARGET}

CLEANFILES+=	${MFILE}

.if defined(IS_PRIMARY)
MLINKS+=	${MFILE} c++filt.1
.endif

.include <bsd.prog.mk>
