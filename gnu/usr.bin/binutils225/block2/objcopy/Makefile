.include "../../Makefile.inc1"
.include "../Makefile.block2"

.PATH: ${SRCDIR}/binutils ${SRCDIR}/binutils/doc

PROG=	objcopy
SRCS=	objcopy.c not-strip.c rename.c ${WRITE_DEBUG_SRCS}
MFILE=	objcopy${MANPAGEVER}.1
MAN=	${MFILE}
LDADD=	${STD_LDADD} -lz

DEBUG_SRCS=		rddbg.c debug.c stabs.c ieee.c rdcoff.c
WRITE_DEBUG_SRCS=	${DEBUG_SRCS} wrstabs.c

${MFILE}: ${SRCDIR}/binutils/doc/objcopy.1
	cp ${.ALLSRC} ${.TARGET}

CLEANFILES+=	${MFILE}

.if defined(IS_PRIMARY)
MLINKS+=	${MFILE} objcopy.1
.endif

.include <bsd.prog.mk>
