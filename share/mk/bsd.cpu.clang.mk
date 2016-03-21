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

known_x86_64=	x86_64 i386
known_i386=	i386

. if defined(known_${MACHINE_ARCH}) && \
     !empty(known_${MACHINE_ARCH}:M${CPUTYPE})		# CID: Check CPUTYPE

# i386 used for 32-bit BIOS-based boot loaders

C_x86_64=	${generic_x86_64}
C_i386=		${generic_i386}

_CPUCFLAGS=	-march=${CPUTYPE}
MACHINE_CPU=	${C_${CPUTYPE:S|-||}}

. else							# CID: Check CPUTYPE

# CPUTYPE was defined and was not empty, but the value does not match known
# CPU types of the defined MACHINE_ARCH.  Set -march to native and define
# generic features based on MACHINE_ARCH

_CPUCFLAGS=	-march=native
MACHINE_CPU=	${generic_${MACHINE_ARCH}}

. endif							# CID: Check CPUTYPE

.endif
