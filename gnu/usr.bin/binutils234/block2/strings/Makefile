.include "../../Makefile.inc1"
.include "../Makefile.block2"

.PATH: ${SRCDIR}/binutils

PROG=	strings
MFILE=	strings${MANPAGEVER}.1
MAN=	${MFILE}
LDADD=	${STD_LDADD} -lz

.if defined(IS_PRIMARY)
MLINKS+=	${MFILE} strings.1
.endif

.include <bsd.prog.mk>
