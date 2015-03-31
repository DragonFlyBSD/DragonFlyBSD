.include "../../Makefile.inc1"
.include "../Makefile.block2"

.PATH: ${SRCDIR}/binutils ${SRCDIR}/binutils/doc

PROG=	objdump
SRCS=	objdump.c dwarf.c prdbg.c $(DEBUG_SRCS) elfcomm.c
MFILE=	objdump${MANPAGEVER}.1
MAN=	${MFILE}
LDADD=	${STD_LDADD} ${LDADD_OPCODE} -lz

DEBUG_SRCS=		rddbg.c debug.c stabs.c ieee.c rdcoff.c
CFLAGS+=		-I${.OBJDIR}
FLAGS_GROUPS=		objdump
objdump_FLAGS=		-DOBJDUMP_PRIVATE_VECTORS=""
objdump_FLAGS_FILES=	objdump.c

dwarf.c: bfd_stdint.h

${MFILE}: ${SRCDIR}/binutils/doc/objdump.1
	cp ${.ALLSRC} ${.TARGET}

CLEANFILES+=	${MFILE} bfd_stdint.h

.if defined(IS_PRIMARY)
MLINKS+=	${MFILE} objdump.1
.endif

.include <bsd.prog.mk>
