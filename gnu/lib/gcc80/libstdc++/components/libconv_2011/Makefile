RELUP=	/../..
.include "../../Makefile.inc"
SRCDIR=	${GCCDIR}/libstdc++-v3
.include "Makefile.src"

LIB=			c++11convenience
INTERNALLIB=		yes
INTERNALLIBPROF=	yes
INSTALL_PIC_ARCHIVE=	yes

WARNS=	1

.PATH:	${SRCDIR}
.PATH:	${SRCDIR}/src
.PATH:	${SRCDIR}/src/c++11
.PATH:  ${SRCDIR}/config/os/bsd/dragonfly
.PATH:	${SRCDIR}/config/locale/dragonfly
.PATH:	${SRCDIR}/config/locale/generic

# for now do not let the use of -flto (-Wodr...)
.if ${CFLAGS:M-flto}
CFLAGS+=	-fno-lto
LDFLAGS+=	-flto
.endif

CFLAGS+=	-I${.OBJDIR}
CXXFLAGS+=	-std=gnu++11 -fdiagnostics-show-location=once
CXXFLAGS+=	-ffunction-sections -fdata-sections
SHARED_CXXFLAGS=	-D_GLIBCXX_SHARED

SRCS=	${libc__11convenience_la_SOURCES}
INTHD=	${SRCDIR}/config/locale/generic/c++locale_internal.h
GENHD=	cxxabi_forced.h ${INTHD:T}

FLAG_GROUPS=		implicit explicit
implicit_FLAGS=		-fimplicit-templates
implicit_FLAGS_FILES=	hashtable_c++0x.cc
explicit_FLAGS=		-fno-implicit-templates
explicit_FLAGS_FILES=	${SRCS:Nhashtable_c++0x.cc}

# PR libstdc++/85222 needs intermediate manipulation
OBJS+=	cxx11-ios_failure.o
S1DUAL=	'/^_*_ZTISt13__ios_failure:/,/_ZTVN10__cxxabiv120__si_class_type_infoE/s/_ZTVN10__cxxabiv120__si_class_type_infoE/_ZTVSt19__iosfail_type_info/'
IOSTMP=	cxx11-ios_failure.*.s*

_COMPILE_IOS_FAILURE: .USE
	${COMPILE.cc} ${.IMPSRC} ${explicit_FLAGS} -S -o ${.TARGET}.s.tmp
	sed -e ${S1DUAL} < ${.TARGET}.s.tmp > ${.TARGET}.s
	${COMPILE.cc} ${.TARGET}.s ${explicit_FLAGS} -g0 -o ${.TARGET}

cxx11-ios_failure.o cxx11-ios_failure.po cxx11-ios_failure.So: _COMPILE_IOS_FAILURE

cxxabi_forced.h: ${SRCDIR}/libsupc++/cxxabi_forced.h
	cp ${.ALLSRC} ${.TARGET}

${INTHD:T}: copybits ${INTHD}
	cp ${INTHD} bits/${.TARGET}

copybits:
	mkdir -p bits

CLEANDIRS=	bits
CLEANFILES=	${GENHD} ${IOSTMP}

beforedepend:	${GENHD}

.include <bsd.lib.mk>
