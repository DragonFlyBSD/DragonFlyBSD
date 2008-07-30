# $DragonFly: src/gnu/usr.bin/gdb/libopcodes/Makefile,v 1.2 2008/07/30 01:48:14 corecode Exp $

.include "../Makefile.inc"

LIB=	opcodes
INTERNALLIB=	true

CONTRIBDIR=	${BASEDIR}/opcodes
.PATH:	${CONTRIBDIR}

CFLAGS+=	-I${BASEDIR}/include -I${.CURDIR} -I${.CURDIR}/../libbfd -I${.OBJDIR}/../libbfd

SRCS=	dis-buf.c dis-init.c disassemble.c

.include "Makefile.${MACHINE_ARCH}"

.include <bsd.lib.mk>
