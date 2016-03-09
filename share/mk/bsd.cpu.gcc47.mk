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
# https://gcc.gnu.org/onlinedocs/gcc-4.7.4/gcc/i386-and-x86-64-Options.html
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
		atom \
		barcelona \
		bdver1 \
		bdver2 \
		btver1 \
		core-avx-i \
		core2 \
		corei7 \
		corei7-avx \
		i386 \
		nocona \
		znver1

known_i386=	i386

. if defined(known_${MACHINE_ARCH}) && \
     !empty(known_${MACHINE_ARCH}:M${CT2})		# CID: Check CPUTYPE

# Set up the list of CPU features based on the CPU type.  This is an
# unordered list to make it easy for client makefiles to test for the
# presence of a CPU feature.

.  if ${MACHINE_ARCH} == "x86_64"
C_i386=		${generic_i386}
C_nocona=	${generic_x86_64} sse3
C_core2=	${C_nocona} ssse3
C_corei7=	${C_core2} sse41 sse42
C_corei7avx=	${C_corei7} avx aes pclmul
C_atom=		${C_core2}
C_athlon64=	${generic_x86_64} 3dnow
C_athlon64sse3=	${C_athlon64} sse3
C_barcelona=	${C_athlon64sse3} sse4a abm
C_bdver1=	${C_corei7avx} sse4a abm fma4 xop lwp cx16
C_bdver2=	${C_bdver1} bmi f16c fma tbm
C_btver1=	${C_barcelona} cx16
C_coreavxi=	${C_corei7avx} fsgsbase rdrnd f16c
C_znver1=	${C_corei7avx} bmi bmi2 f16c fma fsgsbase avx2 adcx rdseed \
		mwaitx sha clzero cx16 movbe sse4a abm xsavec xsaves \
		clflushop popcnt

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
