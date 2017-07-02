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

cxxabi_forced.h: ${SRCDIR}/libsupc++/cxxabi_forced.h
	cp ${.ALLSRC} ${.TARGET}

${INTHD:T}: copybits ${INTHD}
	cp ${INTHD} bits/${.TARGET}

copybits:
	mkdir -p bits

CLEANDIRS=	bits
CLEANFILES=	${GENHD}

beforedepend:	${GENHD}

.include <bsd.lib.mk>
