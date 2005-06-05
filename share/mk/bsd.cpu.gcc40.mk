# $DragonFly: src/share/mk/Attic/bsd.cpu.gcc40.mk,v 1.1 2005/06/05 22:43:19 corecode Exp $

# Set default CPU compile flags and baseline CPUTYPE for each arch.  The
# compile flags must support the minimum CPU type for each architecture but
# may tune support for more advanced processors.

.if !defined(CPUTYPE) || empty(CPUTYPE)
. if ${MACHINE_ARCH} == "i386"
_CPUCFLAGS = -mtune=pentiumpro
MACHINE_CPU = i486
.elif ${MACHINE_ARCH} == "amd64"
MACHINE_CPU = amd64 sse2 sse
. endif
.else

# Handle aliases (not documented in make.conf to avoid user confusion
# between e.g. i586 and pentium)

. if ${MACHINE_ARCH} == "i386"
.  if ${CPUTYPE} == "pentiumpro"
CPUTYPE = i686
.  elif ${CPUTYPE} == "pentium"
CPUTYPE = i586
.  elif ${CPUTYPE} == "k7"
CPUTYPE = athlon
.  endif
. endif

# Logic to set up correct gcc optimization flag.  This must be included
# after /etc/make.conf so it can react to the local value of CPUTYPE
# defined therein.  Consult:
#	http://gcc.gnu.org/onlinedocs/gcc/i386-and-x86-64-Options.html
#	http://gcc.gnu.org/onlinedocs/gcc/DEC-Alpha-Options.html
#	http://gcc.gnu.org/onlinedocs/gcc/SPARC-Options.html
#	http://gcc.gnu.org/onlinedocs/gcc/RS-6000-and-PowerPC-Options.html

. if ${MACHINE_ARCH} == "i386"
.  if ${CPUTYPE} == "k8" || ${CPUTYPE} == "opteron" || ${CPUTYPE} == "athlon64" || ${CPUTYPE} == "athlon-fx"
_CPUCFLAGS = -march=${CPUTYPE}
.  elif ${CPUTYPE} == "athlon-mp" || ${CPUTYPE} == "athlon-xp" || ${CPUTYPE} == "athlon-4" || ${CPUTYPE} == "athlon-tbird" || ${CPUTYPE} == "athlon"
_CPUCFLAGS = -march=${CPUTYPE}
.  elif ${CPUTYPE} == "k6-3" || ${CPUTYPE} == "k6-2" || ${CPUTYPE} == "k6"
_CPUCFLAGS = -march=${CPUTYPE}
.  elif ${CPUTYPE} == "k5"
_CPUCFLAGS = -march=pentium
.  elif ${CPUTYPE} == "p4"
_CPUCFLAGS = -march=pentium4
.  elif ${CPUTYPE} == "p3"
_CPUCFLAGS = -march=pentium3
.  elif ${CPUTYPE} == "p2"
_CPUCFLAGS = -march=pentium2
.  elif ${CPUTYPE} == "i686"
_CPUCFLAGS = -march=pentiumpro
.  elif ${CPUTYPE} == "i586/mmx"
_CPUCFLAGS = -march=pentium-mmx
.  elif ${CPUTYPE} == "i586"
_CPUCFLAGS = -march=pentium
.  elif ${CPUTYPE} == "i486"
_CPUCFLAGS = -march=i486
.  endif
. endif

# Set up the list of CPU features based on the CPU type.  This is an
# unordered list to make it easy for client makefiles to test for the
# presence of a CPU feature.

.if ${MACHINE_ARCH} == "i386"
. if ${CPUTYPE} == "k8" || ${CPUTYPE} == "opteron" || ${CPUTYPE} == "athlon64" || ${CPUTYPE} == "athlon-fx"
MACHINE_CPU = athlon-xp k7 3dnow sse mmx k6 k5 i586 i486 i386
. elif ${CPUTYPE} == "athlon-mp" || ${CPUTYPE} == "athlon-xp" || ${CPUTYPE} == "athlon-4"
MACHINE_CPU = athlon-xp k7 3dnow sse mmx k6 k5 i586 i486 i386
. elif ${CPUTYPE} == "athlon" || ${CPUTYPE} == "athlon-tbird"
MACHINE_CPU = athlon k7 3dnow mmx k6 k5 i586 i486 i386
. elif ${CPUTYPE} == "k6-3" || ${CPUTYPE} == "k6-2"
MACHINE_CPU = 3dnow mmx k6 k5 i586 i486 i386
.  elif ${CPUTYPE} == "k6"
MACHINE_CPU = mmx k6 k5 i586 i486 i386
.  elif ${CPUTYPE} == "k5"
MACHINE_CPU = k5 i586 i486 i386
.  elif ${CPUTYPE} == "p4"
MACHINE_CPU = sse2 sse i686 mmx i586 i486 i386
.  elif ${CPUTYPE} == "p3"
MACHINE_CPU = sse i686 mmx i586 i486 i386
.  elif ${CPUTYPE} == "p2"
MACHINE_CPU = i686 mmx i586 i486 i386
.  elif ${CPUTYPE} == "i686"
MACHINE_CPU = i686 i586 i486 i386
.  elif ${CPUTYPE} == "i586/mmx"
MACHINE_CPU = mmx i586 i486 i386
.  elif ${CPUTYPE} == "i586"
MACHINE_CPU = i586 i486 i386
.  elif ${CPUTYPE} == "i486"
MACHINE_CPU = i486 i386
.  elif ${CPUTYPE} == "i386"
MACHINE_CPU = i386
.  endif
. elif ${MACHINE_ARCH} == "amd64"
MACHINE_CPU = amd64 sse2 sse
. endif
.endif

