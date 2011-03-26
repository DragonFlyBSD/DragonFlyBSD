.if !target(__<cc.Makefile.inc>__)
__<cc.Makefile.inc>__:

GCCCOMPLETEVER=	4.4.5
GCCPOINTVER=	${GCCCOMPLETEVER:R}
GCCSHORTVER=	${GCCPOINTVER:S/.//}

GCCDOCSUFFIX=	${GCCSHORTVER}

CSTD?=		gnu89

CFLAGS+=	-DGCCPOINTVER=\"${GCCPOINTVER}\"
CFLAGS+=	-DGCCSHORTVER=\"${GCCSHORTVER}\"
CFLAGS+=	-DBASEVER=\"${GCCCOMPLETEVER}\"

CFLAGS+=	-DDEFAULT_TARGET_VERSION=\"${version}\"
CFLAGS+=	-DDEFAULT_TARGET_MACHINE=\"${target_machine}\"

BINDIR?=	/usr/libexec/gcc${GCCSHORTVER}
.include "../Makefile.inc"

GCCDIR=		${.CURDIR}/${TOP_PREFIX}/../../../../contrib/gcc-${GCCPOINTVER}
OTOPDIR=	${.OBJDIR}/${TOP_PREFIX}/..
STOPDIR=	${.CURDIR}/${TOP_PREFIX}/..

TOOLDIR=	${OTOPDIR}/cc_tools/tools

CFLAGS+=	-I${OTOPDIR}/cc_prep -I${STOPDIR}/cc_prep -I${STOPDIR}/cc_prep/config
CFLAGS+=	-I${GCCDIR}/gcc -I${GCCDIR}/gcc/config -I${GCCDIR}/include
CFLAGS+=	-I${GCCDIR}/libcpp/include
CFLAGS+=	-I${GCCDIR}/libdecnumber -I${GCCDIR}/libdecnumber/dpd
CFLAGS+=	-I${OTOPDIR}/libdecnumber
CFLAGS+=	-I${STOPDIR}/gmp
CFLAGS+=	-I${.CURDIR}/${TOP_PREFIX}/../../../../contrib/mpfr -I${STOPDIR}/mpfr
CFLAGS+=	-I${GCCDIR}/gcc/config/${GCC_CPU} -I${TOOLDIR}

CFLAGS+=	-DIN_GCC -DHAVE_CONFIG_H
CFLAGS+=	-DPREFIX=\"${TOOLS_PREFIX}/usr\"
CFLAGS+=	-DPREFIX2=\"${USRDATA_PREFIX}/usr\"

.include "Makefile.tgt"

.if ${TARGET_ARCH} != ${MACHINE_ARCH}
CFLAGS+=	-DCROSS_COMPILE
.endif

.if defined(GCC_LANG_DIR)
.PATH:	${GCCDIR}/${GCC_LANG_DIR}
.endif

.if !defined(GCC_NO_PATH)
.PATH:	${OTOPDIR}/cc_prep ${STOPDIR}/cc_prep ${GCCDIR}/gcc
.PATH:	${GCCDIR}/gcc/config/${GCC_CPU} ${TOOLDIR}
.endif

LIBIBERTY=	${OTOPDIR}/libiberty/libiberty.a
LIBBACKEND=	${OTOPDIR}/libbackend/libbackend.a
LIBCPP=		${OTOPDIR}/libcpp/libcpp.a
LIBDECNUMBER=	${OTOPDIR}/libdecnumber/libdecnumber.a

LIBGMP=		${OTOPDIR}/gmp/libgmp.a
LIBMPFR=	${OTOPDIR}/mpfr/libmpfr.a

GMPLIBS=	${LIBMPFR} ${LIBGMP}
STDLIBS=	${LIBCPP} ${LIBIBERTY} ${LIBDECNUMBER} 
BACKENDLIBS=	${GMPLIBS}

.if !defined(GCC_NO_LIBS)
LDADD+=	${STDLIBS}
DPADD+=	${STDLIBS}
.endif

.endif
