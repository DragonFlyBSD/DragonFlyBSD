.include "../../Makefile.inc1"
.include "../Makefile.block2"

.PATH: ${SRCDIR}/binutils ${SRCDIR}/binutils/doc

PROG=	ranlib
SRCS=	ar.c is-ranlib.c arparse.y arlex.l arsup.c rename.c binemul.c \
	emul_vanilla.c
MFILE=	ranlib${MANPAGEVER}.1
MAN=	${MFILE}
DPADD=	${LIBL} ${LIBZ}
LDADD=	${STD_LDADD} -ll -lz

CFLAGS+=	-I${SRCDIR}/binutils \
		-Dbin_dummy_emulation=bin_vanilla_emulation

# link-time optimization requires dlopen() which means ranlib cannot be built
# statically and also support -plugin, not even for cross-tools.
.undef NOSHARED

${MFILE}: ${SRCDIR}/binutils/doc/ranlib.1
	cp ${.ALLSRC} ${.TARGET}

CLEANFILES+=	${MFILE}

.if defined(IS_PRIMARY)
MLINKS+=	${MFILE} ranlib.1
.endif

.include <bsd.prog.mk>
