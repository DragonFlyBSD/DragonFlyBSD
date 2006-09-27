# $DragonFly: src/gnu/lib/gcc41/Makefile.inc,v 1.1 2006/09/27 12:10:33 corecode Exp $

TOP_PREFIX=	../../../usr.bin/cc41/cc_tools
CCVER=gcc41

TARGET_LIBDIR=		${LIBDIR}/gcc41
TARGET_DEBUGLIBDIR=	${LIBDIR}/gcc41/debug
TARGET_PROFLIBDIR=	${LIBDIR}/gcc41/profile
TARGET_SHLIBDIR=	${LIBDIR}/gcc41

.include "../../usr.bin/cc41/Makefile.inc"
