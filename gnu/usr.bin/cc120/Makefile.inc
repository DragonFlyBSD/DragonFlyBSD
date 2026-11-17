.if !target(__<cc.Makefile.inc>__)
__<cc.Makefile.inc>__:

.include "Makefile.version"

GCCDIR=		${.CURDIR}/${TOP_PREFIX}../../../../contrib/gcc-12
OTOPDIR=	${.OBJDIR}/${TOP_PREFIX}..
STOPDIR=	${.CURDIR}/${TOP_PREFIX}..

OSLDIR=		${OTOPDIR}/support-libs
TOOLDIR=	${OTOPDIR}/cc_tools/tools

BINDIR?=	/usr/libexec/gcc${GCCSHORTVER}

PREFIX1=			${TOOLS_PREFIX}/usr
PREFIX2=			${USRDATA_PREFIX}/usr
STANDARD_EXEC_PREFIX=		${PREFIX1}/libexec/gcc${GCCSHORTVER}/
GCC_INCLUDE_DIR=		${PREFIX2}/libdata/gcc${GCCSHORTVER}
NATIVE_SYSTEM_HEADER_DIR=	${PREFIX2}/include
GPLUSPLUS_INCLUDE_DIR=		${PREFIX2}/include/c++/${GCCPOINTVER}
GPLUSPLUS_INCLUDE_DIR_ADD_SYSROOT= 0

# Tailored from the same-named macro from gcc/Makefile.in
# Used by cppdefault.o.
#
# - PREFIX: defined in cc_prep/config/dragonfly-native.h
# - FIXED_INCLUDE_DIR: undefined
# - TOOL_INCLUDE_DIR: undefined
# - LOCAL_INCLUDE_DIR: undefined
# - CROSS_INCLUDE_DIR: undefined
# - GPLUSPLUS_TOOL_INCLUDE_DIR: undefined
# - GPLUSPLUS_LIBCXX_INCLUDE_DIR: undefined (for libc++)
# - GPLUSPLUS_LIBCXX_INCLUDE_DIR_ADD_SYSROOT: undefined (for libc++)
#
PREPROCESSOR_DEFINES= \
	-DGCC_INCLUDE_DIR=\"${GCC_INCLUDE_DIR}\" \
	-DGPLUSPLUS_INCLUDE_DIR=\"${GPLUSPLUS_INCLUDE_DIR}\" \
	-DGPLUSPLUS_INCLUDE_DIR_ADD_SYSROOT=${GPLUSPLUS_INCLUDE_DIR_ADD_SYSROOT} \
	-DGPLUSPLUS_BACKWARD_INCLUDE_DIR=\"${GPLUSPLUS_INCLUDE_DIR}/backward\" \
	-DNATIVE_SYSTEM_HEADER_DIR=\"${NATIVE_SYSTEM_HEADER_DIR}\" \
	-DSTANDARD_EXEC_PREFIX=\"${STANDARD_EXEC_PREFIX}\"

# Tailored from the same-named macro from gcc/Makefile.in
# Used by gcc.o, gcc-ar.o, gcc-nm.o and gcc-ranlib.o.
#
# - STANDARD_STARTFILE_PREFIX: unused when STARTFILE_PREFIX_SPEC is specified
#
DRIVER_DEFINES=	\
	-DSTANDARD_STARTFILE_PREFIX=\"*dummy*\" \
	-DSTANDARD_EXEC_PREFIX=\"${STANDARD_EXEC_PREFIX}\" \
	-DSTANDARD_LIBEXEC_PREFIX=\"${STANDARD_EXEC_PREFIX}\" \
	-DDEFAULT_TARGET_VERSION=\"${version}\" \
	-DDEFAULT_REAL_TARGET_MACHINE=\"${target_machine}\" \
	-DDEFAULT_TARGET_MACHINE=\"${target_machine}\" \
	-DSTANDARD_BINDIR_PREFIX=\"${STANDARD_EXEC_PREFIX}\" \
	-DTOOLDIR_BASE_PREFIX=\"./\" \
	-DACCEL_DIR_SUFFIX=\"\" \
	-DCONFIGURE_SPECS=\"\" \
	-DNATIVE_SYSTEM_HEADER_DIR=\"${NATIVE_SYSTEM_HEADER_DIR}\"

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

FLAGS=		-DGCCPOINTVER=\"${GCCPOINTVER}\"
FLAGS+=		-DGCCSHORTVER=\"${GCCSHORTVER}\"
FLAGS+=		-DBASEVER=\"${GCCCOMPLETEVER}\"
FLAGS+=		-DDATESTAMP=\"${GCCDATESTAMP}\"

.if defined(LOCAL_CONFIG)
FLAGS+=		-I${.CURDIR}
FLAGS+=		-I${.OBJDIR}
.endif

FLAGS+=		-I${OTOPDIR}/cc_prep
FLAGS+=		-I${STOPDIR}/cc_prep
FLAGS+=		-I${STOPDIR}/cc_prep/config
FLAGS+=		-I${GCCDIR}/gcc
FLAGS+=		-I${GCCDIR}/gcc/config
FLAGS+=		-I${GCCDIR}/gcc/config/i386
FLAGS+=		-I${TOOLDIR}
FLAGS+=		-I${GCCDIR}/include

.if !defined(GCC_NO_INCS)
FLAGS+=		-I${GCCDIR}/libcody
FLAGS+=		-I${GCCDIR}/libcpp/include
FLAGS+=		-I${GCCDIR}/libdecnumber
FLAGS+=		-I${GCCDIR}/libdecnumber/dpd
FLAGS+=		-I${OSLDIR}/libdecnumber
FLAGS+=		-I${STOPDIR}/../gmp
FLAGS+=		-I${STOPDIR}/../../../contrib/mpfr/src
FLAGS+=		-I${STOPDIR}/../mpfr
FLAGS+=		-I${STOPDIR}/../../../contrib/mpc/src
.endif

FLAGS+=		-DIN_GCC -DHAVE_CONFIG_H
FLAGS+=		-DPREFIX1=\"${PREFIX1}\"
FLAGS+=		-DPREFIX2=\"${PREFIX2}\"

.if defined(BOOTSTRAPPING)
FLAGS+=		-DCTOOLS
.endif

CFLAGS+=	${FLAGS}

.include "Makefile.tgt"

.if ${TARGET_ARCH} != ${MACHINE_ARCH}
CFLAGS+=	-DCROSS_COMPILE
.endif

# needed to complile hsa/brig stubs
NOEXCEPTION_FLAGS=	-fno-exceptions -fno-rtti

LIBIBERTY=	${OSLDIR}/libiberty/libiberty.a
LIBCODY=	${OSLDIR}/libcody/libcody.a
LIBCPP=		${OSLDIR}/libcpp/libcpp.a
LIBDECNUMBER=	${OSLDIR}/libdecnumber/libdecnumber.a
LIBCOMMON=	${OSLDIR}/libcommon/libcommon.a
LIBCOMMONTARG=	${OSLDIR}/libcommon-target/libcommon-target.a
LIBBACKTRACE=	${OSLDIR}/libbacktrace/libbacktrace.a
LIBBACKEND=	${OTOPDIR}/libbackend/libbackend.a

LIBGMP=		${OTOPDIR}/../gmp/lib/libgmp.a
LIBMPFR=	${OTOPDIR}/../mpfr/libmpfr.a
LIBMPC=		${OTOPDIR}/../mpc/libmpc.a
LIBZ=		${OTOPDIR}/../../../lib/libz/libz.a

GMPLIBS=	${LIBMPC} ${LIBMPFR} ${LIBGMP}
STDLIBS=	${LIBCOMMONTARG} ${LIBCOMMON} ${LIBCPP} \
		${LIBBACKTRACE} ${LIBIBERTY} ${LIBDECNUMBER}
BACKENDLIBS=	${GMPLIBS} ${LIBZ} ${CROSS_LIBDL}

.if !defined(GCC_NO_LIBS)
LDADD+=	${STDLIBS}
DPADD+=	${STDLIBS}
.endif

WARNS=	2

.endif
