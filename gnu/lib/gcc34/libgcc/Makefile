# $DragonFly: src/gnu/lib/gcc34/libgcc/Makefile,v 1.5 2004/12/20 19:23:26 joerg Exp $

.include "../Makefile.inc"
.include "${STOPDIR}/cc_tools/Makefile.tools"

CFLAGS+=	-I.
CFLAGS+=	-fexceptions
CFLAGS+=	-DIN_LIBGCC2 -D__GCC_FLOAT_NOT_NEEDED -DHAVE_GTHR_DEFAULT
CFLAGS+=	-DIN_GCC

LIB=		gcc

# used by shared libs
INSTALL_PIC_ARCHIVE=	yes

FUNCS=		__gcc_bcmp __main _absvdi2 _absvsi2 _addvdi3 _addvsi3
FUNCS+=		_ashldi3 _ashrdi3 _clear_cache _clz _clzdi2 _clzsi2
FUNCS+=		_cmpdi2 _ctors _ctzdi2 _ctzsi2 _divdi3 _eprintf _ffsdi2
FUNCS+=		_ffssi2 _fixdfdi _fixsfdi _fixtfdi _fixunsdfdi _fixunsdfsi
FUNCS+=		_fixunssfdi _fixunssfsi _fixunstfdi _fixunsxfdi _fixunsxfsi
FUNCS+=		_fixxfdi _floatdidf _floatdisf _floatditf _floatdixf _gcov
FUNCS+=		_gcov_merge_add _gcov_merge_delta _gcov_merge_single _lshrdi3
FUNCS+=		_moddi3 _muldi3 _mulvdi3 _mulvsi3 _negdi2 _negvdi2
FUNCS+=		_negvsi2 _paritydi2 _paritysi2 _popcount_tab _popcountdi2
FUNCS+=		_popcountsi2 _subvdi3 _subvsi3 _trampoline _ucmpdi2
FUNCS+=		_udiv_w_sdiv _udivdi3 _udivmoddi4 _umoddi3

SRCS+=		${FUNCS:S/$/.c/g}
CLEANFILES+=	${FUNCS:S/$/.c/g}
SRCS+=		gthr-gnat.c unwind-c.c unwind-dw2-fde.c unwind-dw2.c
SRCS+=		unwind-sjlj.c
SRCS+=		insn-flags.h insn-constants.h


.for F in ${FUNCS}
${F}.c:
	echo '#define L${.TARGET:T:R}' > ${.TARGET}
	echo '#include "libgcc2.c"' >> ${.TARGET}
.endfor

.for F in _moddi3.c _udivdi3.c _umoddi3.c _udivmoddi4.c
${F:R:S/$/.o/}: ${F}
	${CC} ${CFLAGS} -fnon-call-exceptions -c ${.IMPSRC}
.endfor

depend: cc_prep cc_tools
.ORDER: cc_prep cc_tools ${SRCS}

# This is kinda messy, but it's easier then rearranging the top level
# build.  gcc-3.4's libgcc generates source files which depend on generated
# header files, which means we have to generate our tools set before
# we can build libgcc.  The toolset is normally built afterwords.
#
# XXX for the world stage we could theoretically use the cc_tools generated
# in the btools stage, but it's easier to regenerate them.
#
cc_prep: .PHONY
	cd ${STOPDIR}/cc_prep; ${MAKE} depend

cc_tools: .PHONY
	cd ${STOPDIR}/cc_tools; ${MAKE} depend

.if ${MACHINE_ARCH} == "i386"
.PATH: ${GCCDIR}/gcc/config/i386

INCS=	emmintrin.h mmintrin.h pmmintrin.h xmmintrin.h
INCSDIR=	/usr/libdata/gcc34
.endif

.include <bsd.lib.mk>
