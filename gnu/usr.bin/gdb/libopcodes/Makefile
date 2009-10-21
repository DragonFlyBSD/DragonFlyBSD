.include "../Makefile.inc"

LIB=	opcodes
INTERNALLIB=	true

CONTRIBDIR=	${BASEDIR}/opcodes
.PATH:	${CONTRIBDIR}

CFLAGS+=	-I${BASEDIR}/include -I${.CURDIR} -I${.CURDIR}/../libbfd -I${.OBJDIR}/../libbfd

SRCS=	dis-buf.c dis-init.c disassemble.c

.include "Makefile.${MACHINE_ARCH}"

.include <bsd.lib.mk>
