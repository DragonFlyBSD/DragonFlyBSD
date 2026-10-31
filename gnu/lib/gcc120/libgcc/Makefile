# GCC's libgcc generates source files which depend on generated
# header files, which means we have to generate our tools set before
# we can build libgcc.  The toolset is normally built afterwards.
#
# Assumed to be built prior to this makefile:
#   gnu/usr.bin/cc120/cc_prep
#   gnu/usr.bin/cc120/cc_tools
#   gnu/lib/gcc120/csu
#

.include "../Makefile.inc"
.include "Makefile.src"

LIB=		gcc

# this is a special case
NOSHARED=	YES

WARNS=		1

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

OBJS=		${LIB2OBJS} ${LIB2OBJS_ST}
SRCS=		${LIB2ADD:T}

FLAGS_GROUPS=		sse2
sse2_FLAGS=		-msse2
sse2_FLAGS_FILES=	${sse2_files}

# generated sources
SRCS+=		gthr-default.h sfp-machine.h unwind.h

# Derived from gcc-build/gcc/Makefile, excluding the standard headers already
# provided by the system (e.g., stdint.h, tgmath.h).
#
# List of additional header files to install.
EXTRA_HEADERS= \
	$(GCCDIR)/gcc/config/i386/cpuid.h \
	$(GCCDIR)/gcc/config/i386/mmintrin.h \
	$(GCCDIR)/gcc/config/i386/mm3dnow.h \
	$(GCCDIR)/gcc/config/i386/xmmintrin.h \
	$(GCCDIR)/gcc/config/i386/emmintrin.h \
	$(GCCDIR)/gcc/config/i386/pmmintrin.h \
	$(GCCDIR)/gcc/config/i386/tmmintrin.h \
	$(GCCDIR)/gcc/config/i386/ammintrin.h \
	$(GCCDIR)/gcc/config/i386/smmintrin.h \
	$(GCCDIR)/gcc/config/i386/nmmintrin.h \
	$(GCCDIR)/gcc/config/i386/bmmintrin.h \
	$(GCCDIR)/gcc/config/i386/fma4intrin.h \
	$(GCCDIR)/gcc/config/i386/wmmintrin.h \
	$(GCCDIR)/gcc/config/i386/immintrin.h \
	$(GCCDIR)/gcc/config/i386/x86intrin.h \
	$(GCCDIR)/gcc/config/i386/avxintrin.h \
	$(GCCDIR)/gcc/config/i386/xopintrin.h \
	$(GCCDIR)/gcc/config/i386/ia32intrin.h \
	$(GCCDIR)/gcc/config/i386/cross-stdarg.h \
	$(GCCDIR)/gcc/config/i386/lwpintrin.h \
	$(GCCDIR)/gcc/config/i386/popcntintrin.h \
	$(GCCDIR)/gcc/config/i386/lzcntintrin.h \
	$(GCCDIR)/gcc/config/i386/bmiintrin.h \
	$(GCCDIR)/gcc/config/i386/bmi2intrin.h \
	$(GCCDIR)/gcc/config/i386/tbmintrin.h \
	$(GCCDIR)/gcc/config/i386/avx2intrin.h \
	$(GCCDIR)/gcc/config/i386/avx512fintrin.h \
	$(GCCDIR)/gcc/config/i386/fmaintrin.h \
	$(GCCDIR)/gcc/config/i386/f16cintrin.h \
	$(GCCDIR)/gcc/config/i386/rtmintrin.h \
	$(GCCDIR)/gcc/config/i386/xtestintrin.h \
	$(GCCDIR)/gcc/config/i386/rdseedintrin.h \
	$(GCCDIR)/gcc/config/i386/prfchwintrin.h \
	$(GCCDIR)/gcc/config/i386/adxintrin.h \
	$(GCCDIR)/gcc/config/i386/fxsrintrin.h \
	$(GCCDIR)/gcc/config/i386/xsaveintrin.h \
	$(GCCDIR)/gcc/config/i386/xsaveoptintrin.h \
	$(GCCDIR)/gcc/config/i386/avx512cdintrin.h \
	$(GCCDIR)/gcc/config/i386/avx512erintrin.h \
	$(GCCDIR)/gcc/config/i386/avx512pfintrin.h \
	$(GCCDIR)/gcc/config/i386/shaintrin.h \
	$(GCCDIR)/gcc/config/i386/clflushoptintrin.h \
	$(GCCDIR)/gcc/config/i386/xsavecintrin.h \
	$(GCCDIR)/gcc/config/i386/xsavesintrin.h \
	$(GCCDIR)/gcc/config/i386/avx512dqintrin.h \
	$(GCCDIR)/gcc/config/i386/avx512bwintrin.h \
	$(GCCDIR)/gcc/config/i386/avx512vlintrin.h \
	$(GCCDIR)/gcc/config/i386/avx512vlbwintrin.h \
	$(GCCDIR)/gcc/config/i386/avx512vldqintrin.h \
	$(GCCDIR)/gcc/config/i386/avx512ifmaintrin.h \
	$(GCCDIR)/gcc/config/i386/avx512ifmavlintrin.h \
	$(GCCDIR)/gcc/config/i386/avx512vbmiintrin.h \
	$(GCCDIR)/gcc/config/i386/avx512vbmivlintrin.h \
	$(GCCDIR)/gcc/config/i386/avx5124fmapsintrin.h \
	$(GCCDIR)/gcc/config/i386/avx5124vnniwintrin.h \
	$(GCCDIR)/gcc/config/i386/avx512vpopcntdqintrin.h \
	$(GCCDIR)/gcc/config/i386/clwbintrin.h \
	$(GCCDIR)/gcc/config/i386/mwaitxintrin.h \
	$(GCCDIR)/gcc/config/i386/clzerointrin.h \
	$(GCCDIR)/gcc/config/i386/pkuintrin.h \
	$(GCCDIR)/gcc/config/i386/sgxintrin.h \
	$(GCCDIR)/gcc/config/i386/cetintrin.h \
	$(GCCDIR)/gcc/config/i386/gfniintrin.h \
	$(GCCDIR)/gcc/config/i386/cet.h \
	$(GCCDIR)/gcc/config/i386/avx512vbmi2intrin.h \
	$(GCCDIR)/gcc/config/i386/avx512vbmi2vlintrin.h \
	$(GCCDIR)/gcc/config/i386/avx512vnniintrin.h \
	$(GCCDIR)/gcc/config/i386/avx512vnnivlintrin.h \
	$(GCCDIR)/gcc/config/i386/vaesintrin.h \
	$(GCCDIR)/gcc/config/i386/vpclmulqdqintrin.h \
	$(GCCDIR)/gcc/config/i386/avx512vpopcntdqvlintrin.h \
	$(GCCDIR)/gcc/config/i386/avx512bitalgintrin.h \
	$(GCCDIR)/gcc/config/i386/pconfigintrin.h \
	$(GCCDIR)/gcc/config/i386/wbnoinvdintrin.h \
	$(GCCDIR)/gcc/config/i386/movdirintrin.h \
	$(GCCDIR)/gcc/config/i386/waitpkgintrin.h \
	$(GCCDIR)/gcc/config/i386/cldemoteintrin.h \
	$(GCCDIR)/gcc/config/i386/avx512bf16vlintrin.h \
	$(GCCDIR)/gcc/config/i386/avx512bf16intrin.h \
	$(GCCDIR)/gcc/config/i386/enqcmdintrin.h \
	$(GCCDIR)/gcc/config/i386/serializeintrin.h \
	$(GCCDIR)/gcc/config/i386/avx512vp2intersectintrin.h \
	$(GCCDIR)/gcc/config/i386/avx512vp2intersectvlintrin.h \
	$(GCCDIR)/gcc/config/i386/tsxldtrkintrin.h \
	$(GCCDIR)/gcc/config/i386/amxtileintrin.h \
	$(GCCDIR)/gcc/config/i386/amxint8intrin.h \
	$(GCCDIR)/gcc/config/i386/amxbf16intrin.h \
	$(GCCDIR)/gcc/config/i386/x86gprintrin.h \
	$(GCCDIR)/gcc/config/i386/uintrintrin.h \
	$(GCCDIR)/gcc/config/i386/hresetintrin.h \
	$(GCCDIR)/gcc/config/i386/keylockerintrin.h \
	$(GCCDIR)/gcc/config/i386/avxvnniintrin.h \
	$(GCCDIR)/gcc/config/i386/mwaitintrin.h \
	$(GCCDIR)/gcc/config/i386/avx512fp16intrin.h \
	$(GCCDIR)/gcc/config/i386/avx512fp16vlintrin.h
# Header files that are made available under the same name
# to programs compiled with GCC.
USER_H=	\
	$(GCCDIR)/gcc/ginclude/stdfix.h \
	$(GCCDIR)/gcc/ginclude/stdatomic.h \
	$(EXTRA_HEADERS)

CLEANFILES+=	mm_malloc.h
mm_malloc.h: ${GCCDIR}/gcc/config/i386/gmm_malloc.h
	cp ${.ALLSRC} ${.TARGET}

FILES=		${USER_H} mm_malloc.h unwind.h
FILESDIR=	/usr/libdata/gcc${GCCSHORTVER}

.include <bsd.lib.mk>
