# $DragonFly: src/gnu/lib/gcc41/Makefile.inc,v 1.2 2007/01/20 18:24:34 corecode Exp $

TOP_PREFIX=	../../../usr.bin/cc41/cc_tools
CCVER=gcc41

TARGET_LIBDIR=		${LIBDIR}/gcc41
TARGET_DEBUGLIBDIR=	${LIBDIR}/gcc41/debug
TARGET_PROFLIBDIR=	${LIBDIR}/gcc41/profile
TARGET_SHLIBDIR=	${LIBDIR}/gcc41

GCC_NO_LIBERTY=		# yes

.include "../../usr.bin/cc41/Makefile.inc"
