.include "../../Makefile.inc1"
.include "../Makefile.block2"

.PATH: ${SRCDIR}/binutils ${SRCDIR}/binutils/doc

PROG=	nm
SRCS=	nm.c
MFILE=	nm${MANPAGEVER}.1
MAN=	${MFILE}
LDADD=	${STD_LDADD} -lz

# link-time optimization requires dlopen() which means nm cannot be built
# statically and also support -plugin, not even for cross-tools.
.undef NOSHARED

${MFILE}: ${SRCDIR}/binutils/doc/nm.1
	cp ${.ALLSRC} ${.TARGET}

CLEANFILES+=	${MFILE}

.if defined(IS_PRIMARY)
MLINKS+=	${MFILE} nm.1
.endif

.include <bsd.prog.mk>
