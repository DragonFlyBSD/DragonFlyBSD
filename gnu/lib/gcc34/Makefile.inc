# $DragonFly: src/gnu/lib/gcc34/Makefile.inc,v 1.2 2005/07/28 19:19:47 joerg Exp $

TOP_PREFIX=	../../../usr.bin/cc34/cc_tools
CCVER=gcc34

TARGET_LIBDIR?=		${LIBDIR}/gcc34
TARGET_SHLIBDIR?=	${SHLIBDIR}/gcc34

.include "../../usr.bin/cc34/Makefile.inc"
