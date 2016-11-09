.include "../../Makefile.inc1"
.include "../Makefile.block2"

.PATH: ${SRCDIR}/binutils ${SRCDIR}/binutils/doc

PROG=	ar
SRCS=	ar.c not-ranlib.c arsup.c rename.c binemul.c emul_vanilla.c \
	arparse.y arlex.l
MFILE=	ar${MANPAGEVER}.1
MAN=	${MFILE}
DPADD=	${LIBL} ${LIBZ}
LDADD=	${STD_LDADD} -ll -lz

CFLAGS+=	-I${SRCDIR}/binutils \
		-Dbin_dummy_emulation=bin_vanilla_emulation

# link-time optimization requires dlopen() which means ar cannot be built
# statically and also support -plugin, not even for cross-tools.
.undef NOSHARED

${MFILE}: ${SRCDIR}/binutils/doc/ar.1
	cp ${.ALLSRC} ${.TARGET}

CLEANFILES+=	${MFILE}

.if defined(IS_PRIMARY)
MLINKS+=	${MFILE} ar.1
.endif

.include <bsd.prog.mk>
