# $DragonFly: src/gnu/lib/gcc40/Attic/Makefile.inc,v 1.2 2005/07/28 19:19:47 joerg Exp $

TOP_PREFIX=	../../../usr.bin/cc40/cc_tools
CCVER=gcc40

TARGET_LIBDIR?=		${LIBDIR}/gcc40
TARGET_SHLIBDIR?=	${SHLIBDIR}/gcc40

.include "../../usr.bin/cc40/Makefile.inc"
