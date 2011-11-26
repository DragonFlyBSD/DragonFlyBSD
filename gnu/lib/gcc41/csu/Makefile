# $DragonFly: src/gnu/lib/gcc41/csu/Makefile,v 1.1 2007/01/15 18:01:44 corecode Exp $

GCCDIR=	${.CURDIR}/../../../../contrib/gcc-4.1
CCDIR=	${.CURDIR}/../../../usr.bin/cc41

.include "../Makefile.inc"

CFLAGS+=	-finhibit-size-directive
CFLAGS+=	-fno-inline-functions
CFLAGS+=	-fno-exceptions
CFLAGS+=	-fno-zero-initialized-in-bss
CFLAGS+=	-fno-unit-at-a-time
CFLAGS+=	-fno-omit-frame-pointer
CFLAGS+=	-fno-asynchronous-unwind-tables
CFLAGS+=	-I.

.include "../../csu/Makefile.csu"
