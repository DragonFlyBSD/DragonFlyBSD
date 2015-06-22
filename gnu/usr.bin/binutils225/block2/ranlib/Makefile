.include "../../Makefile.inc1"
.include "../Makefile.block2"

.PATH: ${SRCDIR}/binutils ${SRCDIR}/binutils/doc

PROG=	ranlib
SRCS=	ar.c is-ranlib.c arparse.y arlex.l arsup.c rename.c binemul.c \
	emul_vanilla.c
MFILE=	ranlib${MANPAGEVER}.1
MAN=	${MFILE}
LDADD=	${STD_LDADD} -lfl -lz

NOSHARED?=	yes
CFLAGS+=	-I${SRCDIR}/binutils \
		-Dbin_dummy_emulation=bin_vanilla_emulation

${MFILE}: ${SRCDIR}/binutils/doc/ranlib.1
	cp ${.ALLSRC} ${.TARGET}

CLEANFILES+=	${MFILE}

.if defined(IS_PRIMARY)
MLINKS+=	${MFILE} ranlib.1
.endif

.include <bsd.prog.mk>
