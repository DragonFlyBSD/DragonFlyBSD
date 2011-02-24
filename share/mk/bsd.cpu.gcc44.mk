# Set default CPU compile flags and baseline CPUTYPE for each arch.  The
# compile flags must support the minimum CPU type for each architecture but
# may tune support for more advanced processors.

.if !defined(CPUTYPE) || empty(CPUTYPE) || ${CPUTYPE} == "native"

. if defined(CPUTYPE) && !empty(CPUTYPE) && ${CPUTYPE} == "native"
_CPUCFLAGS = -march=native
. else
_CPUCFLAGS =
. endif

. if ${MACHINE_ARCH} == "i386"
MACHINE_CPU = i486 i386
. elif ${MACHINE_ARCH} == "x86_64"
MACHINE_CPU = x86_64 sse2 sse mmx
. endif

.else

_CPUCFLAGS_FIXUP =

# Old CPUTYPE compat shim
. if ${MACHINE_ARCH} == "i386"
.  if ${CPUTYPE} == "core"
CPUTYPE = nocona
.  elif ${CPUTYPE} == "p4"
CPUTYPE = pentium4
.  elif ${CPUTYPE} == "p4m"
CPUTYPE = pentium4m
.  elif ${CPUTYPE} == "p3"
CPUTYPE = pentium3
.  elif ${CPUTYPE} == "p3m"
CPUTYPE = pentium3m
.  elif ${CPUTYPE} == "p-m"
CPUTYPE = pentium-m
.  elif ${CPUTYPE} == "p2"
CPUTYPE = pentium2
.  elif ${CPUTYPE} == "i586/mmx"
CPUTYPE = pentium-mmx
.  elif ${CPUTYPE} == "k5"
CPUTYPE = pentium
.  elif ${CPUTYPE} == "k7"
CPUTYPE = athlon
.  elif ${CPUTYPE} == "crusoe"
CPUTYPE = pentiumpro
_CPUCFLAGS_FIXUP = -falign-functions=0 -falign-jumps=0 -falign-loops=0
.  endif
. elif ${MACHINE_ARCH} == "x86_64"
.  if ${CPUTYPE} == "prescott"
CPUTYPE = nocona
.  endif
. endif

# CPUTYPE alias
. if ${CPUTYPE} == "i586"
CPUTYPE = pentium
. elif ${CPUTYPE} == "i686"
CPUTYPE = pentiumpro
. elif ${CPUTYPE} == "pentium3m"
CPUTYPE = pentium3
. elif ${CPUTYPE} == "pentium4m"
CPUTYPE = pentium4
. elif ${CPUTYPE} == "k6-2"
CPUTYPE = k6-3
. elif ${CPUTYPE} == "athlon-tbird"
CPUTYPE = athlon
. elif ${CPUTYPE} == "athlon-mp" || ${CPUTYPE} == "athlon-4"
CPUTYPE = athlon-xp
. elif ${CPUTYPE} == "k8" || ${CPUTYPE} == "opteron" || \
       ${CPUTYPE} == "athlon-fx"
CPUTYPE = athlon64
. elif ${CPUTYPE} == "k8-sse3" || ${CPUTYPE} == "opteron-sse3"
CPUTYPE = athlon64-sse3
. elif ${CPUTYPE} == "amdfam10"
CPUTYPE = barcelona
. endif

###############################################################################
# Logic to set up correct gcc optimization flag.  This must be included
# after /etc/make.conf so it can react to the local value of CPUTYPE
# defined therein.  Consult:
#	http://gcc.gnu.org/onlinedocs/gcc/i386-and-x86-64-Options.html

_CPUCFLAGS = -march=${CPUTYPE} ${_CPUCFLAGS_FIXUP}

# Set up the list of CPU features based on the CPU type.  This is an
# unordered list to make it easy for client makefiles to test for the
# presence of a CPU feature.

. if ${MACHINE_ARCH} == "i386"
.  if ${CPUTYPE} == "barcelona"
MACHINE_CPU = athlon-xp athlon k7 3dnow abm sse4a sse3 sse2 sse mmx k6 k5 \
	      i586 i486 i386
.  elif ${CPUTYPE} == "athlon64-sse3"
MACHINE_CPU = athlon-xp athlon k7 3dnow sse3 sse2 sse mmx k6 k5 i586 i486 i386
.  elif ${CPUTYPE} == "athlon64"
MACHINE_CPU = athlon-xp athlon k7 3dnow sse2 sse mmx k6 k5 i586 i486 i386
.  elif ${CPUTYPE} == "athlon-xp"
MACHINE_CPU = athlon-xp athlon k7 3dnow sse mmx k6 k5 i586 i486 i386
.  elif ${CPUTYPE} == "athlon"
MACHINE_CPU = athlon k7 3dnow mmx k6 k5 i586 i486 i386
.  elif ${CPUTYPE} == "k6-3"
MACHINE_CPU = 3dnow mmx k6 k5 i586 i486 i386
.  elif ${CPUTYPE} == "k6"
MACHINE_CPU = mmx k6 k5 i586 i486 i386
.  elif ${CPUTYPE} == "c3"
MACHINE_CPU = 3dnow mmx i586 i486 i386
.  elif ${CPUTYPE} == "c3-2"
MACHINE_CPU = sse mmx i586 i486 i386
.  elif ${CPUTYPE} == "winchip2"
MACHINE_CPU = 3dnow mmx i486 i386
.  elif ${CPUTYPE} == "winchip-c6"
MACHINE_CPU = mmx i486 i386
.  elif ${CPUTYPE} == "core2"
MACHINE_CPU = ssse3 sse3 sse2 sse i686 mmx i586 i486 i386
.  elif ${CPUTYPE} == "prescott" || ${CPUTYPE} == "nocona"
MACHINE_CPU = sse3 sse2 sse i686 mmx i586 i486 i386
.  elif ${CPUTYPE} == "pentium4" || ${CPUTYPE} == "pentium-m"
MACHINE_CPU = sse2 sse i686 mmx i586 i486 i386
.  elif ${CPUTYPE} == "pentium3"
MACHINE_CPU = sse i686 mmx i586 i486 i386
.  elif ${CPUTYPE} == "pentium2"
MACHINE_CPU = i686 mmx i586 i486 i386
.  elif ${CPUTYPE} == "pentiumpro"
MACHINE_CPU = i686 i586 i486 i386
.  elif ${CPUTYPE} == "pentium-mmx"
MACHINE_CPU = mmx i586 i486 i386
.  elif ${CPUTYPE} == "pentium"
MACHINE_CPU = i586 i486 i386
.  elif ${CPUTYPE} == "i486"
MACHINE_CPU = i486 i386
.  elif ${CPUTYPE} == "i386"
MACHINE_CPU = i386
.  endif
. elif ${MACHINE_ARCH} == "x86_64"
.  if ${CPUTYPE} == "barcelona"
MACHINE_CPU = k8 3dnow abm sse4a sse3
.  elif ${CPUTYPE} == "athlon64-sse3"
MACHINE_CPU = k8 3dnow sse3
.  elif ${CPUTYPE} == "athlon64"
MACHINE_CPU = k8 3dnow
.  elif ${CPUTYPE} == "core2"
MACHINE_CPU = ssse3 sse3
.  elif ${CPUTYPE} == "nocona"
MACHINE_CPU = sse3
.  endif
MACHINE_CPU += x86_64 sse2 sse mmx
. endif

.endif
