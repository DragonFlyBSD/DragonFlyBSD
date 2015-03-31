.include "../Makefile.block1"

LIB=		opcodes
INTERNALLIB=	true

CONTRIBDIR=	${SRCDIR}/opcodes
.PATH:		${CONTRIBDIR}

CFLAGS+=		-I${.OBJDIR}
FLAGS_GROUPS=		assy
assy_FLAGS=		-DARCH_i386 -DARCH_l1om -DARCH_k1om
assy_FLAGS_FILES=	disassemble.c

SRCS=	i386-dis.c i386-opc.c dis-buf.c dis-init.c disassemble.c

i386-dis.c disassemble.c dis-buf.c: bfd.h

CLEANFILES=	bfd.h

.include <bsd.lib.mk>
