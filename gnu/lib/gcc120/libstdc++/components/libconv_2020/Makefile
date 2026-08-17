RELUP=	/../..
.include "../../Makefile.inc"
SRCDIR=	${GCCDIR}/libstdc++-v3
.include "Makefile.src"

LIB=			c++20convenience
INTERNALLIB=		yes
INTERNALLIBPROF=	yes
# build the PIC archive for libstdc++.so
INSTALL_PIC_ARCHIVE=	yes

.PATH:	${SRCDIR}/src/c++20

CXXFLAGS+=	-std=gnu++20 -fimplicit-templates

SRCS=	${libc__20convenience_la_SOURCES}

.include <bsd.lib.mk>
