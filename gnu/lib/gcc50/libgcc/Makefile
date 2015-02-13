.include "../Makefile.inc"
.include "Makefile.src"
.PATH: ${GCCDIR}/libgcc
.PATH: ${GCCDIR}/libgcc/soft-fp
.PATH: ${GCCDIR}/libgcc/config/i386

LIB=		gcc

CFLAGS+=	-I${.CURDIR}
CFLAGS+=	-I${.OBJDIR}
CFLAGS+=	-I${GCCDIR}/libgcc
CFLAGS+=	-I${GCCDIR}/libgcc/config/i386
CFLAGS+=	-I../csu
CFLAGS+=	-fbuilding-libgcc
CFLAGS+=	-fno-stack-protector
CFLAGS+=	-fvisibility=hidden
CFLAGS+=	-fpic
CFLAGS+=	-DPIC
CFLAGS+=	-DIN_GCC
CFLAGS+=	-DIN_LIBGCC2
CFLAGS+=	-DHAVE_CC_TLS
CFLAGS+=	-DHIDE_EXPORTS

# FUNCS and SOFTFUNCS defined in Makefile.src
OBJS=		${FUNCS:S/$/.o/}
SRCS=		${SOFTFUNCS:S/$/.c/}

#generated sources
SRCS+=		gthr-default.h sfp-machine.h

# GCC's libgcc generates source files which depend on generated
# header files, which means we have to generate our tools set before
# we can build libgcc.  The toolset is normally built afterwards.
#
# XXX for the world stage we could theoretically use the cc_tools generated
# in the btools stage, but it's easier to regenerate them.
#
# Assumed to be built prior to this makefile:
#   gnu/usr.bin/cc50/cc_prep
#   gnu/usr.bin/cc50/cc_tools
#   gnu/lib/gcc50/csu

# derived from generated build/gcc/Makefile
EXTRA_HEADERS = \
	${GCCDIR}/gcc/config/i386/cpuid.h \
	${GCCDIR}/gcc/config/i386/mmintrin.h \
	${GCCDIR}/gcc/config/i386/mm3dnow.h \
	${GCCDIR}/gcc/config/i386/xmmintrin.h \
	${GCCDIR}/gcc/config/i386/emmintrin.h \
	${GCCDIR}/gcc/config/i386/pmmintrin.h \
	${GCCDIR}/gcc/config/i386/tmmintrin.h \
	${GCCDIR}/gcc/config/i386/ammintrin.h \
	${GCCDIR}/gcc/config/i386/smmintrin.h \
	${GCCDIR}/gcc/config/i386/nmmintrin.h \
	${GCCDIR}/gcc/config/i386/bmmintrin.h \
	${GCCDIR}/gcc/config/i386/fma4intrin.h \
	${GCCDIR}/gcc/config/i386/wmmintrin.h \
	${GCCDIR}/gcc/config/i386/immintrin.h \
	${GCCDIR}/gcc/config/i386/x86intrin.h \
	${GCCDIR}/gcc/config/i386/avxintrin.h \
	${GCCDIR}/gcc/config/i386/xopintrin.h \
	${GCCDIR}/gcc/config/i386/ia32intrin.h \
	${GCCDIR}/gcc/config/i386/cross-stdarg.h \
	${GCCDIR}/gcc/config/i386/lwpintrin.h \
	${GCCDIR}/gcc/config/i386/popcntintrin.h \
	${GCCDIR}/gcc/config/i386/lzcntintrin.h \
	${GCCDIR}/gcc/config/i386/bmiintrin.h \
	${GCCDIR}/gcc/config/i386/tbmintrin.h \
	${GCCDIR}/gcc/config/i386/bmi2intrin.h \
	${GCCDIR}/gcc/config/i386/avx2intrin.h \
	${GCCDIR}/gcc/config/i386/avx512fintrin.h \
	${GCCDIR}/gcc/config/i386/fmaintrin.h \
	${GCCDIR}/gcc/config/i386/f16cintrin.h \
	${GCCDIR}/gcc/config/i386/rtmintrin.h \
	${GCCDIR}/gcc/config/i386/xtestintrin.h \
	${GCCDIR}/gcc/config/i386/rdseedintrin.h \
	${GCCDIR}/gcc/config/i386/prfchwintrin.h \
	${GCCDIR}/gcc/config/i386/adxintrin.h \
	${GCCDIR}/gcc/config/i386/fxsrintrin.h \
	${GCCDIR}/gcc/config/i386/xsaveintrin.h \
	${GCCDIR}/gcc/config/i386/xsaveoptintrin.h \
	${GCCDIR}/gcc/config/i386/avx512cdintrin.h \
	${GCCDIR}/gcc/config/i386/avx512erintrin.h \
	${GCCDIR}/gcc/config/i386/avx512pfintrin.h \
	${GCCDIR}/gcc/config/i386/shaintrin.h \
	${GCCDIR}/gcc/config/i386/clflushoptintrin.h \
	${GCCDIR}/gcc/config/i386/xsavecintrin.h \
	${GCCDIR}/gcc/config/i386/xsavesintrin.h \
	${GCCDIR}/gcc/config/i386/avx512dqintrin.h \
	${GCCDIR}/gcc/config/i386/avx512bwintrin.h \
	${GCCDIR}/gcc/config/i386/avx512vlintrin.h \
	${GCCDIR}/gcc/config/i386/avx512vlbwintrin.h \
	${GCCDIR}/gcc/config/i386/avx512vldqintrin.h \
	${GCCDIR}/gcc/config/i386/avx512ifmaintrin.h \
	${GCCDIR}/gcc/config/i386/avx512ifmavlintrin.h \
	${GCCDIR}/gcc/config/i386/avx512vbmiintrin.h \
	${GCCDIR}/gcc/config/i386/avx512vbmivlintrin.h \
	${GCCDIR}/gcc/config/i386/clwbintrin.h \
	${GCCDIR}/gcc/config/i386/pcommitintrin.h \
	${GCCDIR}/gcc/ginclude/tgmath.h

#generated sources
SRCS+=	mm_malloc.h unwind.h

INCS=		${EXTRA_HEADERS} mm_malloc.h unwind.h
INCSDIR=	/usr/libdata/gcc${GCCSHORTVER}

mm_malloc.h: ${GCCDIR}/gcc/config/i386/gmm_malloc.h
	cp ${.ALLSRC} ${.TARGET}

gthr-default.h: gthr-posix.h
	cp ${.ALLSRC} ${.TARGET}

sfp-machine.h: ${GCCDIR}/libgcc/config/i386/sfp-machine.h
	cp ${.ALLSRC} ${.TARGET}

enable-execute-stack.c: enable-execute-stack-mprotect.c
	cp ${.ALLSRC} ${.TARGET}

unwind.h: unwind-generic.h
	cp ${.ALLSRC} ${.TARGET}

CLEANFILES+=	mm_malloc.h unwind.h gthr-default.h enable-execute-stack.c

.include <bsd.lib.mk>
