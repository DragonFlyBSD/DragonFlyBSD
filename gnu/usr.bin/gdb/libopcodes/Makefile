# $DragonFly: src/gnu/usr.bin/gdb/libopcodes/Makefile,v 1.1 2004/10/23 17:02:38 joerg Exp $

.include "../Makefile.inc"

LIB=	opcodes
INTERNALLIB=	true

CONTRIBDIR=	${BASEDIR}/opcodes
.PATH:	${CONTRIBDIR}

CFLAGS+=	-I${BASEDIR}/include -I${.CURDIR} -I${.CURDIR}/../libbfd

SRCS=	dis-buf.c dis-init.c disassemble.c

.include "Makefile.${MACHINE_ARCH}"

.include <bsd.lib.mk>
