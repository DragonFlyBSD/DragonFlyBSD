# $DragonFly: src/gnu/lib/gcc34/Makefile.inc,v 1.3 2005/07/30 13:59:45 joerg Exp $

TOP_PREFIX=	../../../usr.bin/cc34/cc_tools
CCVER=gcc34

TARGET_LIBDIR=		${LIBDIR}/gcc34
TARGET_DEBUGLIBDIR=	${LIBDIR}/gcc34/debug
TARGET_PROFLIBDIR=	${LIBDIR}/gcc34/profile
TARGET_SHLIBDIR=	${LIBDIR}/gcc34

.include "../../usr.bin/cc34/Makefile.inc"
