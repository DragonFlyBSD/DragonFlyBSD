.include "../../Makefile.inc1"
.include "../Makefile.block2"

.PATH: ${SRCDIR}/binutils

PROG=	readelf
SRCS=	readelf.c version.c unwind-ia64.c dwarf.c elfcomm.c
MFILE=	readelf${MANPAGEVER}.1
MAN=	${MFILE}
# this is a bit of cheat, should link with libctf_nobfd.a (no ctf-open-bfd.o)
# adjust if needed later on (done to avoid to have two separate versions)
LDADD=	${LDADD_CTF} ${LDADD_IBERTY} -lz

CFLAGS+=	-I${.OBJDIR}

.if defined(IS_PRIMARY)
MLINKS+=	${MFILE} readelf.1
.endif

.include <bsd.prog.mk>
