# $DragonFly: src/gnu/lib/gcc41/libgcc/Makefile,v 1.1 2006/09/27 12:10:33 corecode Exp $

.include "../Makefile.inc"
.include "${STOPDIR}/cc_tools/Makefile.tools"

CFLAGS+=	-I.
CFLAGS+=	-fexceptions
CFLAGS+=	-DIN_LIBGCC2 -D__GCC_FLOAT_NOT_NEEDED -DHAVE_GTHR_DEFAULT
CFLAGS+=	-DIN_GCC

LIB=		gcc

# used by shared libs
INSTALL_PIC_ARCHIVE=	yes

FUNCS=	_eprintf __gcc_bcmp \
	_muldi3 _negdi2 _lshrdi3 _ashldi3 _ashrdi3 \
	_cmpdi2 _ucmpdi2 _floatdidf _floatdisf _fixunsdfsi _fixunssfsi \
	_fixunsdfdi _fixdfdi _fixunssfdi _fixsfdi _fixxfdi _fixunsxfdi \
	_floatdixf _fixunsxfsi _fixtfdi _fixunstfdi _floatditf _clear_cache \
	_enable_execute_stack _trampoline __main _absvsi2 _absvdi2 _addvsi3 \
	_addvdi3 _subvsi3 _subvdi3 _mulvsi3 _mulvdi3 _negvsi2 _negvdi2 _ctors \
	_ffssi2 _ffsdi2 _clz _clzsi2 _clzdi2 _ctzsi2 _ctzdi2 _popcount_tab \
	_popcountsi2 _popcountdi2 _paritysi2 _paritydi2 _powisf2 _powidf2 \
	_powixf2 _powitf2 _mulsc3 _muldc3 _mulxc3 _multc3 _divsc3 _divdc3 \
	_divxc3 _divtc3

SRCS+=		${FUNCS:S/$/.c/g}
CLEANFILES+=	${FUNCS:S/$/.c/g}

SRCS+=	unwind-dw2.c unwind-dw2-fde.c unwind-sjlj.c
SRCS+=	gthr-gnat.c unwind-c.c

SRCS+=	insn-flags.h insn-constants.h unwind.h

unwind.h: unwind-generic.h
	cp ${.ALLSRC} ${.TARGET}

CLEANFILES+=	unwind.h

.for F in ${FUNCS}
${F}.c:
	echo '#define L${.TARGET:T:R}' > ${.TARGET}
	echo '#include "libgcc2.c"' >> ${.TARGET}
.endfor

.for F in _divdi3.c _moddi3.c _udivdi3.c _umoddi3.c _udiv_w_sdiv.c _udivmoddi4.c
${F:R:S/$/.o/}: ${F}
	${CC} ${CFLAGS} -fnon-call-exceptions -c ${.IMPSRC}
.endfor

depend: cc_prep cc_tools
.ORDER: cc_prep cc_tools ${SRCS}

# This is kinda messy, but it's easier then rearranging the top level
# build.  gcc-4.1's libgcc generates source files which depend on generated
# header files, which means we have to generate our tools set before
# we can build libgcc.  The toolset is normally built afterwards.
#
# XXX for the world stage we could theoretically use the cc_tools generated
# in the btools stage, but it's easier to regenerate them.
#
cc_prep: .PHONY
	cd ${STOPDIR}/cc_prep; ${MAKE} depend

cc_tools: .PHONY
	cd ${STOPDIR}/cc_tools; ${MAKE} depend

.if ${GCC_CPU} == "i386"
.PATH: ${GCCDIR}/gcc/config/i386

SRCS+=	mm_malloc.h

mm_malloc.h: pmm_malloc.h
	cp ${.ALLSRC} ${.TARGET}

CLEANFILES+=	mm_malloc.h

INCS=	emmintrin.h mmintrin.h pmmintrin.h xmmintrin.h mm_malloc.h
INCSDIR=	/usr/libdata/gcc41
.endif

.include <bsd.lib.mk>
