RELUP=	/../..
.include "../../../Makefile.inc"
SRCDIR=	${GCCDIR}/libstdc++-v3
.include "Makefile.src"

LIB=			cxx11convenience
INTERNALLIB=		yes
INTERNALLIBPROF=	yes
INSTALL_PIC_ARCHIVE=	yes

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
OBJS+=	cxx11-ios_failure_sed.o
S1DUAL=	'/^_*_ZTISt13__ios_failure:/,/_ZTVN10__cxxabiv120__si_class_type_infoE/s/_ZTVN10__cxxabiv120__si_class_type_infoE/_ZTVSt19__iosfail_type_info/'
IOSTMP=	ios_static.s ios_static.tmp ios_po.s ios_po.tmp ios_sh.s ios_sh.tmp

cxx11-ios_failure_sed.o: cxx11-ios_failure.cc
	${CXX} ${explicit_FLAGS} ${STATIC_CXXFLAGS} ${CXXFLAGS} -S -o ios_static.tmp ${.ALLSRC}
	sed -e ${S1DUAL} < ios_static.tmp > ios_static.s
	${CXX} ${explicit_FLAGS} ${STATIC_CXXFLAGS} ${CXXFLAGS} -g0 -c ios_static.s -o ${.TARGET}

cxx11-ios_failure_sed.po: cxx11-ios_failure.cc
	${CXX} ${explicit_FLAGS} ${PO_FLAG} ${STATIC_CXXFLAGS} ${PO_CXXFLAGS} -S -o ios_po.tmp ${.ALLSRC}
	sed -e ${S1DUAL} < ios_po.tmp > ios_po.s
	${CXX} ${explicit_FLAGS} ${PO_FLAG} ${STATIC_CXXFLAGS} ${PO_CXXFLAGS} -g0 -c ios_po.s -o ${.TARGET}

cxx11-ios_failure_sed.So: cxx11-ios_failure.cc
	${CXX} ${explicit_FLAGS} ${PICFLAG} -DPIC ${SHARED_CXXFLAGS} ${CXXFLAGS} -S -o ios_sh.tmp ${.ALLSRC}
	sed -e ${S1DUAL} < ios_sh.tmp > ios_sh.s
	${CXX} ${explicit_FLAGS} ${PICFLAG} -DPIC ${SHARED_CXXFLAGS} ${CXXFLAGS} -g0 -c ios_sh.s -o ${.TARGET}

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
