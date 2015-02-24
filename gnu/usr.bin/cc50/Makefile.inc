.if !target(__<cc.Makefile.inc>__)
__<cc.Makefile.inc>__:

.include "Makefile.version"

FLAGS=		-DGCCPOINTVER=\"${GCCPOINTVER}\"
FLAGS+=		-DGCCSHORTVER=\"${GCCSHORTVER}\"
FLAGS+=		-DBASEVER=\"${GCCCOMPLETEVER}\"

FLAGS+=		-DDEFAULT_TARGET_VERSION=\"${version}\"
FLAGS+=		-DDEFAULT_TARGET_MACHINE=\"${target_machine}\"

BINDIR?=	/usr/libexec/gcc${GCCSHORTVER}

GCCDIR=		${.CURDIR}/${TOP_PREFIX}../../../../contrib/gcc-${GCCPOINTVER}
OTOPDIR=	${.OBJDIR}/${TOP_PREFIX}..
STOPDIR=	${.CURDIR}/${TOP_PREFIX}..
OSLDIR=		${OTOPDIR}/support-libs

TOOLDIR=	${OTOPDIR}/cc_tools/tools

.if defined(LOCAL_CONFIG)
FLAGS+=		-I${.CURDIR}
FLAGS+=		-I${.OBJDIR}
.endif
FLAGS+=		-I${OTOPDIR}/cc_prep
FLAGS+=		-I${STOPDIR}/cc_prep
FLAGS+=		-I${STOPDIR}/cc_prep/config
FLAGS+=		-I${GCCDIR}/gcc
FLAGS+=		-I${GCCDIR}/gcc/config
FLAGS+=		-I${GCCDIR}/include
FLAGS+=		-I${GCCDIR}/libcpp/include
FLAGS+=		-I${GCCDIR}/libdecnumber
FLAGS+=		-I${GCCDIR}/libdecnumber/dpd
FLAGS+=		-I${OSLDIR}/libdecnumber
FLAGS+=		-I${STOPDIR}/../gmp
FLAGS+=		-I${STOPDIR}/../../../contrib/mpfr/src
FLAGS+=		-I${STOPDIR}/../mpfr
FLAGS+=		-I${STOPDIR}/../../../contrib/mpc/src
FLAGS+=		-I${GCCDIR}/gcc/config/i386
FLAGS+=		-I${TOOLDIR}

FLAGS+=		-DIN_GCC -DHAVE_CONFIG_H
FLAGS+=		-DPREFIX1=\"${TOOLS_PREFIX}/usr\"
FLAGS+=		-DPREFIX2=\"${USRDATA_PREFIX}/usr\"

CFLAGS+=	${FLAGS}

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
.PATH: ${GCCDIR}/gcc/c
.PATH: ${GCCDIR}/gcc/c-family
.PATH: ${GCCDIR}/gcc/config 
.PATH: ${GCCDIR}/gcc/config/i386
.PATH: ${TOOLDIR}
.endif

LIBIBERTY=	${OSLDIR}/libiberty/libiberty.a
LIBCPP= 	${OSLDIR}/libcpp/libcpp.a
LIBDECNUMBER=	${OSLDIR}/libdecnumber/libdecnumber.a
LIBCOMMON=	${OSLDIR}/libcommon/libcommon.a
LIBCOMMONTARG=	${OSLDIR}/libcommon-target/libcommon-target.a
LIBBACKTRACE=	${OSLDIR}/libbacktrace/libbacktrace.a
LIBBACKEND=	${OTOPDIR}/libbackend/libbackend.a

LIBGMP= 	${OTOPDIR}/../gmp/libgmp.a
LIBMPFR=	${OTOPDIR}/../mpfr/libmpfr.a
LIBMPC= 	${OTOPDIR}/../mpc/libmpc.a
LIBZ=		${OTOPDIR}/../../../lib/libz/libz.a

GMPLIBS=	${LIBMPC} ${LIBMPFR} ${LIBGMP}
STDLIBS=	${LIBCOMMONTARG} ${LIBCOMMON} ${LIBCPP} \
		${LIBBACKTRACE} ${LIBIBERTY} ${LIBDECNUMBER} 
BACKENDLIBS=	${GMPLIBS} ${LIBZ}

.if !defined(GCC_NO_LIBS)
LDADD+=	${STDLIBS}
DPADD+=	${STDLIBS}
.endif

.endif
