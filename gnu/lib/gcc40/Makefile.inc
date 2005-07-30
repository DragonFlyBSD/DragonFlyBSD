# $DragonFly: src/gnu/lib/gcc40/Attic/Makefile.inc,v 1.3 2005/07/30 13:59:45 joerg Exp $

TOP_PREFIX=	../../../usr.bin/cc40/cc_tools
CCVER=gcc40

TARGET_LIBDIR=		${LIBDIR}/gcc40
TARGET_DEBUGLIBDIR=	${LIBDIR}/gcc40/debug
TARGET_PROFLIBDIR=	${LIBDIR}/gcc40/profile
TARGET_SHLIBDIR=	${LIBDIR}/gcc40

.include "../../usr.bin/cc40/Makefile.inc"
