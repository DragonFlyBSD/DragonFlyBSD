GCCSHORTVER=	44

# We need to go add some random dir, because
# Makefile.inc will append ".."
TOP_PREFIX=	../../../usr.bin/cc${GCCSHORTVER}/cc_tools

CCVER=gcc${GCCSHORTVER}

TARGET_LIBDIR=		${LIBDIR}/gcc${GCCSHORTVER}
TARGET_DEBUGLIBDIR=	${LIBDIR}/gcc${GCCSHORTVER}/debug
TARGET_PROFLIBDIR=	${LIBDIR}/gcc${GCCSHORTVER}/profile
TARGET_SHLIBDIR=	${LIBDIR}/gcc${GCCSHORTVER}

GCC_NO_LIBS=		# yes

LIBSUPCPP=		${.OBJDIR}/../libsupc++/libsupc++.a

.include "../../usr.bin/cc${GCCSHORTVER}/Makefile.inc"
