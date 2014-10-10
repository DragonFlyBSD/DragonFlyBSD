.if !target(__<cc.Makefile.inc>__)
__<cc.Makefile.inc>__:

GCCCOMPLETEVER=	4.7.4
GCCDATESTAMP=	2014-06-12
GCCPOINTVER=	${GCCCOMPLETEVER:R}
GCCSHORTVER=	${GCCPOINTVER:S/.//}

CFLAGS+=	-DGCCPOINTVER=\"${GCCPOINTVER}\"
CFLAGS+=	-DGCCSHORTVER=\"${GCCSHORTVER}\"
CFLAGS+=	-DBASEVER=\"${GCCCOMPLETEVER}\"

CFLAGS+=	-DDEFAULT_TARGET_VERSION=\"${version}\"
CFLAGS+=	-DDEFAULT_TARGET_MACHINE=\"${target_machine}\"

BINDIR?=	/usr/libexec/gcc${GCCSHORTVER}

GCCDIR=		${.CURDIR}/${TOP_PREFIX}../../../../contrib/gcc-${GCCPOINTVER}
OTOPDIR=	${.OBJDIR}/${TOP_PREFIX}..
STOPDIR=	${.CURDIR}/${TOP_PREFIX}..

TOOLDIR=	${OTOPDIR}/cc_tools/tools

.if defined(LOCAL_CONFIG)
CFLAGS+=	-I${.CURDIR}
CFLAGS+=	-I${.OBJDIR}
.endif
CFLAGS+=	-I${OTOPDIR}/cc_prep
CFLAGS+=	-I${STOPDIR}/cc_prep
CFLAGS+=	-I${STOPDIR}/cc_prep/config
CFLAGS+=	-I${GCCDIR}/gcc
CFLAGS+=	-I${GCCDIR}/gcc/config
CFLAGS+=	-I${GCCDIR}/include
CFLAGS+=	-I${GCCDIR}/libcpp/include
CFLAGS+=	-I${GCCDIR}/libdecnumber
CFLAGS+=	-I${GCCDIR}/libdecnumber/dpd
CFLAGS+=	-I${OTOPDIR}/libdecnumber
CFLAGS+=	-I${STOPDIR}/../gmp
CFLAGS+=	-I${STOPDIR}/../../../contrib/mpfr/src
CFLAGS+=	-I${STOPDIR}/../mpfr
CFLAGS+=	-I${STOPDIR}/../../../contrib/mpc/src
CFLAGS+=	-I${GCCDIR}/gcc/config/${GCC_CPU}
CFLAGS+=	-I${TOOLDIR}

CFLAGS+=	-DIN_GCC -DHAVE_CONFIG_H
CFLAGS+=	-DPREFIX1=\"${TOOLS_PREFIX}/usr\"
CFLAGS+=	-DPREFIX2=\"${USRDATA_PREFIX}/usr\"

.include "Makefile.tgt"

.if ${TARGET_ARCH} != ${MACHINE_ARCH}
CFLAGS+= -DCROSS_COMPILE
.endif

.if defined(GCC_LANG_DIR)
.PATH: ${GCCDIR}/${GCC_LANG_DIR}
.endif

.if !defined(GCC_NO_PATH)
.PATH: ${OTOPDIR}/cc_prep
.PATH: ${STOPDIR}/cc_prep
.PATH: ${GCCDIR}/gcc
.PATH: ${GCCDIR}/gcc/c-family
.PATH: ${GCCDIR}/gcc/config 
.PATH: ${GCCDIR}/gcc/config/${GCC_CPU}
.PATH: ${TOOLDIR}
.endif

LIBIBERTY=	${OTOPDIR}/libiberty/libiberty.a
LIBBACKEND=	${OTOPDIR}/libbackend/libbackend.a
LIBCPP= 	${OTOPDIR}/libcpp/libcpp.a
LIBDECNUMBER=	${OTOPDIR}/libdecnumber/libdecnumber.a
LIBCOMMON=	${OTOPDIR}/libcommon/libcommon.a
LIBCOMMONTARG=	${OTOPDIR}/libcommon-target/libcommon-target.a

LIBGMP= 	${OTOPDIR}/../gmp/libgmp.a
LIBMPFR=	${OTOPDIR}/../mpfr/libmpfr.a
LIBMPC= 	${OTOPDIR}/../mpc/libmpc.a
LIBZ=		${OTOPDIR}/../../../lib/libz/libz.a

GMPLIBS=	${LIBMPC} ${LIBMPFR} ${LIBGMP}
STDLIBS=	${LIBCOMMONTARG} ${LIBCOMMON} ${LIBCPP} ${LIBIBERTY} ${LIBDECNUMBER} 
BACKENDLIBS=	${GMPLIBS} ${LIBZ}

.if !defined(GCC_NO_LIBS)
LDADD+=	${STDLIBS}
DPADD+=	${STDLIBS}
.endif

.endif
