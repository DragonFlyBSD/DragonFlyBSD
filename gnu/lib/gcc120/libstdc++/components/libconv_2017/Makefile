RELUP=	/../..
.include "../../Makefile.inc"
SRCDIR=	${GCCDIR}/libstdc++-v3
.include "Makefile.src"

LIB=			c++17convenience
INTERNALLIB=		yes
INTERNALLIBPROF=	yes
# build the PIC archive for libstdc++.so
INSTALL_PIC_ARCHIVE=	yes

.PATH:	${SRCDIR}/src/c++17

CXXFLAGS+=	-std=gnu++17 -fimplicit-templates

SRCS=	${libc__17convenience_la_SOURCES}

.include <bsd.lib.mk>
