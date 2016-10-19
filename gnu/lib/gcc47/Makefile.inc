# make sure this stays the same as gnu/usr.bin/cc47/Makefile.inc
GCCSHORTVER=	47
GCCPOINTVER=	4.7
GCCSHORTDATE=	20130411

GCCDIR=		${.CURDIR}/../../../../contrib/gcc-${GCCPOINTVER}
OCCDIR= 	${.OBJDIR}/../../../usr.bin/cc${GCCSHORTVER}
CCDIR=		${.CURDIR}/../../../usr.bin/cc${GCCSHORTVER}

CCVER=gcc${GCCSHORTVER}

TARGET_ARCH?=	${MACHINE_ARCH}
GCC_CPU?=	${TARGET_ARCH}
TRAMPSIZE=	10
.if ${TARGET_ARCH} == "x86_64"
GCC_CPU=	i386
TRAMPSIZE=	24
.endif

TARGET_LIBDIR=		${LIBDIR}/${CCVER}
TARGET_DEBUGLIBDIR=	${LIBDIR}/${CCVER}/debug
TARGET_PROFLIBDIR=	${LIBDIR}/${CCVER}/profile
TARGET_SHLIBDIR=	${LIBDIR}/${CCVER}

LIBSUPCPP=		${.OBJDIR}/../libsupc++/libsupc++.a

CFLAGS+=	-I${CCDIR}/cc_prep
CFLAGS+=	-I${CCDIR}/cc_prep/config
CFLAGS+=	-I${OCCDIR}/cc_prep
CFLAGS+=	-I${OCCDIR}/cc_tools/tools
CFLAGS+=	-I${GCCDIR}/include
CFLAGS+=	-I${GCCDIR}/gcc
CFLAGS+=	-I${GCCDIR}/gcc/config
CFLAGS+=	-I${GCCDIR}/gcc/config/${GCC_CPU}
CFLAGS+=	-I${CCDIR}/../gmp

# only gcc 4.8 and later has support for -Og
.if defined(WORLD_CCOPTLEVEL) && ${WORLD_CCOPTLEVEL:Mg} && ${CCVER:Mgcc47}
WORLD_CCOPTLEVEL:=	1
.MAKEFLAGS: WORLD_CCOPTLEVEL=1
.endif
