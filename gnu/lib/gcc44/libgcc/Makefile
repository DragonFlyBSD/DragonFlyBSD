.include "../Makefile.inc"

CFLAGS+=	-I.
CFLAGS+=	-fexceptions
CFLAGS+=	-DIN_LIBGCC2 -D__GCC_FLOAT_NOT_NEEDED -DHAVE_GTHR_DEFAULT
CFLAGS+=	-DIN_GCC

LIB=		gcc

# used by shared libs
INSTALL_PIC_ARCHIVE=	yes

lib2funcs = _muldi3 _negdi2 _lshrdi3 _ashldi3 _ashrdi3 _cmpdi2 _ucmpdi2	   \
	    _clear_cache _enable_execute_stack _trampoline __main _absvsi2 \
	    _absvdi2 _addvsi3 _addvdi3 _subvsi3 _subvdi3 _mulvsi3 _mulvdi3 \
	    _negvsi2 _negvdi2 _ctors _ffssi2 _ffsdi2 _clz _clzsi2 _clzdi2  \
	    _ctzsi2 _ctzdi2 _popcount_tab _popcountsi2 _popcountdi2	   \
	    _paritysi2 _paritydi2 _powisf2 _powidf2 _powixf2 _powitf2	   \
	    _mulsc3 _muldc3 _mulxc3 _multc3 _divsc3 _divdc3 _divxc3	   \
	    _divtc3 _bswapsi2 _bswapdi2

.for f in sf df xf
lib2funcs+=	_fixuns$fsi
.endfor
.for f in sf df xf tf
lib2funcs+=	_fix$fdi _fixuns$fdi _float$fdi _floatun$fdi
.endfor

LIB2_DIVMOD_FUNCS = _divdi3 _moddi3 _udivdi3 _umoddi3 _udiv_w_sdiv _udivmoddi4

# from gcc/Makefile.in
LIB2FUNCS_ST = _eprintf __gcc_bcmp


FUNCS=	${lib2funcs} ${LIB2_DIVMOD_FUNCS} ${LIB2FUNCS_ST}

SRCS+=		${FUNCS:S/$/.c/g}
CLEANFILES+=	${FUNCS:S/$/.c/g}

# LIB2ADDEH, gcc/Makefile
SRCS+=	unwind-dw2.c unwind-dw2-fde-glibc.c unwind-sjlj.c
SRCS+=	gthr-gnat.c unwind-c.c

SRCS+=	unwind.h

unwind.h: unwind-generic.h
	cp ${.ALLSRC} ${.TARGET}

CLEANFILES+=	unwind.h

.for F in ${FUNCS}
${F}.c:
	echo '#define L${.TARGET:T:R}' > ${.TARGET}
	echo '#include "libgcc2.c"' >> ${.TARGET}
.endfor

.for F in ${LIB2_DIVMOD_FUNCS}
${F}.o: ${F}.c
	${CC} ${CFLAGS} -fnon-call-exceptions -c ${.IMPSRC}
.endfor

beforedepend: cc_prep cc_tools
.ORDER: cc_prep cc_tools ${SRCS}

# This is kinda messy, but it's easier then rearranging the top level
# build.  gcc's libgcc generates source files which depend on generated
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
EXTRA_HEADERS = $(srcdir)/config/i386/cpuid.h $(srcdir)/config/i386/mmintrin.h \
$(srcdir)/config/i386/mm3dnow.h $(srcdir)/config/i386/xmmintrin.h \
$(srcdir)/config/i386/emmintrin.h $(srcdir)/config/i386/pmmintrin.h \
$(srcdir)/config/i386/tmmintrin.h $(srcdir)/config/i386/ammintrin.h \
$(srcdir)/config/i386/smmintrin.h $(srcdir)/config/i386/nmmintrin.h \
$(srcdir)/config/i386/bmmintrin.h $(srcdir)/config/i386/mmintrin-common.h \
$(srcdir)/config/i386/wmmintrin.h $(srcdir)/config/i386/immintrin.h \
$(srcdir)/config/i386/x86intrin.h $(srcdir)/config/i386/avxintrin.h \
$(srcdir)/config/i386/cross-stdarg.h $(srcdir)/ginclude/tgmath.h

SRCS+=	mm_malloc.h

mm_malloc.h: pmm_malloc.h
	cp ${.ALLSRC} ${.TARGET}

CLEANFILES+=	mm_malloc.h

INCS=		${EXTRA_HEADERS} mm_malloc.h
INCSDIR=	/usr/libdata/gcc${GCCSHORTVER}
.endif

.include <bsd.lib.mk>
