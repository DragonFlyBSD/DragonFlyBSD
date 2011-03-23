.include "../Makefile.inc0"

LIB=	opcodes
INTERNALLIB=	true

CONTRIBDIR=	${SRCDIR}/opcodes
.PATH:	${CONTRIBDIR}

CFLAGS+=	-I${SRCDIR}/include -I${.CURDIR} -I${.CURDIR}/../libbfd -I${.OBJDIR}/../libbfd

SRCS=	dis-buf.c dis-init.c disassemble.c

.include "Makefile.${MACHINE_ARCH}"

.include <bsd.lib.mk>
