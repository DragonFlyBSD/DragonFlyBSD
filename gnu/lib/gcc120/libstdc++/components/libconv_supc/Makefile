RELUP=	/../..
.include "../../Makefile.inc"
SRCDIR=	${GCCDIR}/libstdc++-v3
.include "Makefile.src"

LIB=			supc++convenience
INTERNALLIB=		yes
INTERNALLIBPROF=	yes
# build the PIC archive for libstdc++.so
INSTALL_PIC_ARCHIVE=	yes

.PATH:	${GCCDIR}/libiberty
.PATH:	${SRCDIR}/libsupc++

SRCS=			${libsupc__convenience_la_SOURCES}

CFLAGS+=		-I${GCCDIR}/libgcc
CXXFLAGS+=		-fno-implicit-templates

FLAGS_GROUPS=		iberty
iberty_FLAGS=		-DHAVE_CONFIG_H -DIN_GLIBCPP_V3 \
			-I${.OBJDIR} -I${GCCDIR}/include
iberty_FLAGS_FILES=	cp-demangle.c

# Copy the config.h for cp-demangle.c
GENFILES+=	config.h
config.h: ${CCDIR}/support-libs/libiberty/config.h
	cp ${.ALLSRC} ${.TARGET}

CLEANFILES=	${GENFILES}
beforedepend:	${GENFILES}

.include <bsd.lib.mk>
