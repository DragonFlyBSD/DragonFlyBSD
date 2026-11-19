.if !target(__<gcc120.Makefile.inc>__)
__<gcc120.Makefile.inc>__:

.include <bsd.dragonfly.mk>

GCCDIR=		${DF.SRCTOP}/contrib/gcc-12
OCCDIR=		${DF.OBJTOP}/gnu/usr.bin/cc120
CCDIR=		${DF.SRCTOP}/gnu/usr.bin/cc120

.include "${CCDIR}/Makefile.version"

CCVER=			gcc${GCCSHORTVER}
TARGETDIR=		${LIBDIR}/gcc${GCCSHORTVER}
TARGET_LIBDIR=		${TARGETDIR}
TARGET_DEBUGLIBDIR=	${TARGETDIR}/debug
TARGET_PROFLIBDIR=	${TARGETDIR}/profile
TARGET_SHLIBDIR=	${TARGETDIR}

.if !defined(GCC_NO_INC)
CFLAGS+=	-I${CCDIR}/cc_prep
CFLAGS+=	-I${CCDIR}/cc_prep/config
CFLAGS+=	-I${OCCDIR}/cc_prep
CFLAGS+=	-I${OCCDIR}/cc_tools/tools
CFLAGS+=	-I${GCCDIR}/include
CFLAGS+=	-I${GCCDIR}/gcc
CFLAGS+=	-I${GCCDIR}/gcc/config
CFLAGS+=	-I${GCCDIR}/gcc/config/i386
CFLAGS+=	-I${DF.SRCTOP}/gnu/usr.bin/gmp
.endif

WARNS?=	2

.endif
