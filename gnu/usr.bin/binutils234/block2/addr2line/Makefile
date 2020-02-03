.include "../../Makefile.inc1"
.include "../Makefile.block2"

.PATH: ${SRCDIR}/binutils

PROG=	addr2line
MFILE=	addr2line${MANPAGEVER}.1
MAN=	${MFILE}
LDADD=	${STD_LDADD} -lz

.if defined(IS_PRIMARY)
MLINKS+=	${MFILE} addr2line.1
.endif

.include <bsd.prog.mk>
