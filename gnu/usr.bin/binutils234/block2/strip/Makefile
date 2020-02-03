.include "../../Makefile.inc1"
.include "../Makefile.block2"

.PATH: ${SRCDIR}/binutils

PROG=	strip
SRCS=	objcopy.c is-strip.c rename.c ${WRITE_DEBUG_SRCS}
MFILE=	strip${MANPAGEVER}.1
MAN=	${MFILE}
LDADD=	${STD_LDADD} -lz
# install(1) might call strip(1) on itself
INSTALLFLAGS= -S

DEBUG_SRCS=		rddbg.c debug.c stabs.c rdcoff.c
WRITE_DEBUG_SRCS=	${DEBUG_SRCS} wrstabs.c

.if defined(IS_PRIMARY)
MLINKS+=	${MFILE} strip.1
.endif

.include <bsd.prog.mk>
