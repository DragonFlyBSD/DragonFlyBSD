RELUP=	/../..
.include "../../Makefile.inc"
SRCDIR=	${GCCDIR}/libstdc++-v3
.include "Makefile.src"

LIB=			c++98convenience
INTERNALLIB=		yes
INTERNALLIBPROF=	yes
# build the PIC archive for libstdc++.so
INSTALL_PIC_ARCHIVE=	yes

.PATH:	${SRCDIR}/src/c++98

SRCS=	${libc__98convenience_la_SOURCES}

CXXFLAGS+=		-std=gnu++98 -fno-implicit-templates

FLAGS_GROUPS=		deprecated gnu11 cow concept parallel implicit
deprecated_FLAGS=	-I${SRCDIR}/include/backward -Wno-deprecated
deprecated_FLAGS_FILES=	strstream.cc
gnu11_FLAGS=		-std=gnu++11 -fchar8_t
gnu11_FLAGS_FILES=	locale_init.cc localename.cc
cow_FLAGS=		-D_GLIBCXX_USE_CXX11_ABI=0 -fimplicit-templates
cow_FLAGS_FILES=	${cow_string_host_sources}
concept_FLAGS=		-D_GLIBCXX_CONCEPT_CHECKS -fimplicit-templates
concept_FLAGS_FILES=	concept-inst.cc
parallel_FLAGS=		-D_GLIBCXX_PARALLEL
parallel_FLAGS_FILES=	${parallel_sources}
implicit_FLAGS=		-fimplicit-templates
implicit_FLAGS_FILES=	c++locale.cc

CLEANFILES+=	${GENFILES}
beforedepend:	${GENFILES}

.include <bsd.lib.mk>
