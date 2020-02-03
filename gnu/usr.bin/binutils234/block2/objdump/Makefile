.include "../../Makefile.inc1"
.include "../Makefile.block2"

.PATH: ${SRCDIR}/binutils

PROG=	objdump
SRCS=	objdump.c dwarf.c prdbg.c $(DEBUG_SRCS) elfcomm.c
MFILE=	objdump${MANPAGEVER}.1
MAN=	${MFILE}
LDADD=	${LDADD_CTF} ${STD_LDADD} ${LDADD_OPCODE} -lz

DEBUG_SRCS=		rddbg.c debug.c stabs.c rdcoff.c
CFLAGS+=		-I${.OBJDIR}
FLAGS_GROUPS=		objdump
objdump_FLAGS=		-DOBJDUMP_PRIVATE_VECTORS=""
objdump_FLAGS_FILES=	objdump.c

.if defined(IS_PRIMARY)
MLINKS+=	${MFILE} objdump.1
.endif

.include <bsd.prog.mk>
