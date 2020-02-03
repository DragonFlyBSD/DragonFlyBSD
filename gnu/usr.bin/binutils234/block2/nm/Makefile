.include "../../Makefile.inc1"
.include "../Makefile.block2"

.PATH: ${SRCDIR}/binutils

PROG=	nm
MFILE=	nm${MANPAGEVER}.1
MAN=	${MFILE}
LDADD=	${STD_LDADD} -lz

# link-time optimization requires dlopen() which means nm cannot be built
# statically and also support -plugin, not even for cross-tools.
.if !defined(REALLY_NOSHARED)
.undef NOSHARED
.endif

.if defined(IS_PRIMARY)
MLINKS+=	${MFILE} nm.1
.endif

.include <bsd.prog.mk>
