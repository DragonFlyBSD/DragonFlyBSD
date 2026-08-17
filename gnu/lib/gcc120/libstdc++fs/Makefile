.include "../libstdc++/Makefile.inc"

SRCDIR=	${GCCDIR}/libstdc++-v3
.PATH:	${SRCDIR}/src/filesystem

LIB=		stdc++fs

NOSHARED=	yes

CXXFLAGS+=	-std=gnu++17
# build as PIC to allow being linked with shared libraries
CXXFLAGS+=	-fPIC -DPIC -D_GLIBCXX_SHARED

# From libstdc++-v3/src/filesystem/Makefile.am
cxx11_abi_sources = \
	cow-dir.cc \
	cow-ops.cc \
	cow-path.cc
sources = \
	dir.cc \
	ops.cc \
	path.cc \
	${cxx11_abi_sources}

SRCS=	${sources}

# Headers are installed by libstdc++/headers/Makefile.

.include <bsd.lib.mk>
