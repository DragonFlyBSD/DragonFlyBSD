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
. if ${CPUTYPE} == "i586" \
  || ${CPUTYPE} == "k5"
CPUTYPE = pentium
. elif ${CPUTYPE} == "i586/mmx"
CPUTYPE = pentium-mmx
. elif ${CPUTYPE} == "p2"
CPUTYPE = pentium2
. elif ${CPUTYPE} == "pentium3m" \
    || ${CPUTYPE} == "p3m" \
    || ${CPUTYPE} == "p3" \
    || ${CPUTYPE} == "c3-2"
CPUTYPE = pentium3
. elif ${CPUTYPE} == "pentium4m" \
    || ${CPUTYPE} == "pentium-m" \
    || ${CPUTYPE} == "p-m" \
    || ${CPUTYPE} == "p4"\
    || ${CPUTYPE} == "p4m"
CPUTYPE = pentium4
. elif ${CPUTYPE} == "crusoe"
CPUTYPE = pentiumpro
_CPUCFLAGS_FIXUP = -falign-functions=0 -falign-jumps=0 -falign-loops=0
. elif ${CPUTYPE} == "i686"
CPUTYPE = pentiumpro
. elif ${CPUTYPE} == "k6-2"
CPUTYPE = k6-3
. elif ${CPUTYPE} == "k7" \
    || ${CPUTYPE} == "athlon-tbird"
CPUTYPE = athlon
. elif ${CPUTYPE} == "athlon-mp" \
    || ${CPUTYPE} == "athlon-4"
CPUTYPE = athlon-xp
. elif ${CPUTYPE} == "k8" \
    || ${CPUTYPE} == "opteron" \
    || ${CPUTYPE} == "athlon-fx"
CPUTYPE = athlon64
. elif ${CPUTYPE} == "k8-sse3" \
    || ${CPUTYPE} == "opteron-sse3"
CPUTYPE = athlon64-sse3
. elif ${CPUTYPE} == "amdfam10"
CPUTYPE = barcelona
. elif ${CPUTYPE} == "c3"
CPUTYPE = winchip2
. elif ${CPUTYPE} == "core"
CPUTYPE = nocona
. elif ${CPUTYPE} == "corei7-avx" \
    || ${CPUTYPE} == "corei7-avx-i" \
    || ${CPUTYPE} == "corei7-avx2"
CPUTYPE = corei7
. elif ${CPUTYPE} == "atom"
CPUTYPE = core2
. elif ${CPUTYPE} == "bdver2"
CPUTYPE = bdver1
. elif ${CPUTYPE} == "btver1"
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
.  if ${CPUTYPE} == "bdver1"
MACHINE_CPU = abm 3dnow mmx sse4.2 sse4.1 sse3 sse2 sse \
	athlon-xp athlon k7 k6 k5 i586
.  elif ${CPUTYPE} == "barcelona"
MACHINE_CPU = abm 3dnow mmx sse4a sse3 sse2 sse \
	athlon-xp athlon k7 k6 k5 i586
.  elif ${CPUTYPE} == "athlon64-sse3"
MACHINE_CPU = 3dnow mmx sse3 sse2 sse athlon-xp athlon k7 k6 k5 i586
.  elif ${CPUTYPE} == "athlon64"
MACHINE_CPU = 3dnow mmx sse2 sse athlon-xp athlon k7 k6 k5 i586
.  elif ${CPUTYPE} == "athlon-xp"
MACHINE_CPU = 3dnow mmx sse athlon-xp athlon k7 k6 k5 i586
.  elif ${CPUTYPE} == "athlon"
MACHINE_CPU = 3dnow mmx athlon k7 k6 k5 i586
.  elif ${CPUTYPE} == "k6-3"
MACHINE_CPU = 3dnow mmx k6 k5 i586
.  elif ${CPUTYPE} == "k6"
MACHINE_CPU = mmx k6 k5 i586
.  elif ${CPUTYPE} == "geode"
MACHINE_CPU = 3dnow mmx i686 i586
.  elif ${CPUTYPE} == "corei7"
MACHINE_CPU = sse4.2 sse4.1 ssse3 sse3 sse2 sse mmx i686 i586
.  elif ${CPUTYPE} == "core2"
MACHINE_CPU = ssse3 sse3 sse2 sse mmx i686 i586
.  elif ${CPUTYPE} == "nocona"
MACHINE_CPU = sse3 sse2 sse mmx i686 i586
.  elif ${CPUTYPE} == "prescott"
MACHINE_CPU = sse3 sse2 sse mmx i686 i586
.  elif ${CPUTYPE} == "pentium4"
MACHINE_CPU = sse2 sse mmx i686 i586
.  elif ${CPUTYPE} == "pentium3"
MACHINE_CPU = sse mmx i686 i586
.  elif ${CPUTYPE} == "pentium2"
MACHINE_CPU = mmx i686 i586
.  elif ${CPUTYPE} == "pentiumpro"
MACHINE_CPU = i686 i586
.  elif ${CPUTYPE} == "winchip2"
MACHINE_CPU = 3dnow mmx
.  elif ${CPUTYPE} == "winchip-c6"
MACHINE_CPU = mmx
.  elif ${CPUTYPE} == "pentium-mmx"
MACHINE_CPU = mmx i586
.  elif ${CPUTYPE} == "pentium"
MACHINE_CPU = i586
.  endif
MACHINE_CPU += i386 i486
. elif ${MACHINE_ARCH} == "x86_64"
.  if ${CPUTYPE} == "bdver1"
MACHINE_CPU = k8 abm 3dnow sse4.2 sse4.1 sse3
.  elif ${CPUTYPE} == "barcelona"
MACHINE_CPU = k8 abm 3dnow sse4a sse3
.  elif ${CPUTYPE} == "athlon64-sse3"
MACHINE_CPU = k8 3dnow sse3
.  elif ${CPUTYPE} == "athlon64"
MACHINE_CPU = k8 3dnow
.  elif ${CPUTYPE} == "corei7"
MACHINE_CPU = sse4.2 sse4.1 ssse3 sse3
.  elif ${CPUTYPE} == "core2"
MACHINE_CPU = ssse3 sse3
.  elif ${CPUTYPE} == "nocona"
MACHINE_CPU = sse3
.  endif
MACHINE_CPU += sse2 sse mmx x86_64
. endif

.endif
