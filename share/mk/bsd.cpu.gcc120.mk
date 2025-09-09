# Set default CPU compile flags and baseline CPUTYPE for each arch.  The
# compile flags must support the minimum CPU type for each architecture but
# may tune support for more advanced processors.

generic_x86_64=	x86_64 mmx sse sse2
generic_i386=	i386

.if !defined(CPUTYPE) || empty(CPUTYPE) || ${CPUTYPE} == "native"

. if defined(CPUTYPE) && ${CPUTYPE:Mnative}
_CPUCFLAGS=	-march=native
. else
_CPUCFLAGS=
. endif

MACHINE_CPU=	${generic_${MACHINE_ARCH}}

.else

###############################################################################
# Logic to set up correct gcc optimization flag.  This must be included
# after /etc/make.conf so it can react to the local value of CPUTYPE
# defined therein.  Consult:
#	https://gcc.gnu.org/onlinedocs/gcc-11.5.0/gcc/x86-Options.html
###############################################################################

# Some GCC cpu-types have aliases, rename them to a single identifier
# If the value for cpu-type is not recognized, throw it away and use -native

. if ${CPUTYPE} == "k8" \
  || ${CPUTYPE} == "opteron" \
  || ${CPUTYPE} == "athlon-fx"
CT2=	athlon64
. elif ${CPUTYPE} == "k8-sse3" \
    || ${CPUTYPE} == "opteron-sse3"
CT2=	athlon64-sse3
. elif ${CPUTYPE} == "amdfam10"
CT2=	barcelona
. else
CT2=	${CPUTYPE}
. endif

known_x86_64=	athlon64 \
		athlon64-sse3 \
		barcelona \
		bdver1 bdver2 bdver3 bdver4 \
		bonnell \
		broadwell \
		btver1 btver2 \
		cannonlake \
		core2 \
		haswell \
		i386 \
		icelake-client \
		icelake-server \
		ivybridge \
		knl \
		knm \
		nocona \
		nehalem \
		sandybridge \
		silvermont \
		skylake \
		skylake-avx512 \
		westmere \
		znver1

known_i386=	i386

. if defined(known_${MACHINE_ARCH}) && \
     !empty(known_${MACHINE_ARCH}:M${CT2})		# CID: Check CPUTYPE

# Set up the list of CPU features based on the CPU type.  This is an
# unordered list to make it easy for client makefiles to test for the
# presence of a CPU feature.

.  if ${MACHINE_ARCH} == "x86_64"
C_nocona=	${generic_x86_64} sse3
C_core2=	${C_nocona} ssse3
C_nehalem=	${C_core2} sse41 sse42 popcnt
C_westmere=	${C_nehalem} aes pclmul
C_sandybridge=	${C_westmere} avx
C_ivybridge=	${C_sandybridge} fsgsbase rdrnd f16c
C_haswell=	${C_ivybridge} movbe avx2 fma bmi bmi2
C_broadwell=	${C_haswell} rdseed adcx prefetchw
C_skylake=	${C_broadwell} xsavec xsaves clflushop
C_knl=		${C_broadwell} avx512f avx512pf avx512er avx512cd
C_knm=		${C_knl} avx5124vnniw avx5124fmaps avx512vpopcntdq
C_bonnell=	${C_core2} movbe
C_silvermont=	${C_westmere} movbe rdrnd
C_skylakeavx512=${C_skylake} pku avx512f clwb avx512vl avx512bw avx512dq \
		avx512cd
C_cannonlake=	${C_skylake} pku avx512f avx512vl avx512bw avx512dq avx512cd \
		avx512vbmi avx512ifma sha umip
C_icelakeclient=${C_skylakeavx512} avx512vbmi avx512ifma sha umip rdpid gfni \
		avx512vbmi2 avx512vpopcntdq avx512bitalg avx512vnni \
		vpclmulqdq vaes
C_icelakeserver=${C_icelakeclient} pconfig wbnoinvd

C_athlon64=	${generic_x86_64} 3dnow
C_athlon64sse3=	${C_athlon64} sse3
C_barcelona=	${C_athlon64sse3} sse4a abm
C_bdver1=	${C_core2} sse4a sse41 sse42 abm fma4 avx xop lwp aes \
		pclmul cx16
C_bdver2=	${C_bdver1} bmi f16c fma tbm
C_bdver3=	${C_bdver2} fsgsbase
C_bdver4=	${C_bdver3} bmi2 avx2 movbe
C_znver1=	${C_core2} bmi bmi2 f16c fma fsgsbase avx avx2 adcx rdseed \
		mwaitx sha clzero aes pclmul cx16 movbe sse4a sse41 sse42 \
		abm xsavec xsaves clflushop popcnt
C_btver1=	${C_barcelona} cx16
C_btver2=	${C_btver1} movbe f16c bmi avx pclmul aes sse41 sse42
C_i386=		${generic_i386}

.  endif	# end of x86_64 feature list

# i386 used for 32-bit BIOS-based boot loaders

.  if ${MACHINE_ARCH} == "i386"
C_i386=		${generic_i386}
.  endif	# end of i386 feature list

_CPUCFLAGS=	-march=${CT2}
MACHINE_CPU=	${C_${CT2:S|-||}}

. else							# CID: Check CPUTYPE

# CPUTYPE was defined and was not empty, but the value does not match known
# CPU types of the defined MACHINE_ARCH.  Set -march to native and define
# generic features based on MACHINE_ARCH

_CPUCFLAGS=	-march=native
MACHINE_CPU=	${generic_${MACHINE_ARCH}}

. endif							# CID: Check CPUTYPE

.endif
