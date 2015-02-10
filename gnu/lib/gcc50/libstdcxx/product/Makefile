RELUP=	/..
.include "../../Makefile.inc"
SRCDIR=	${GCCDIR}/libstdc++-v3
.include "Makefile.src"

LIB=		stdc++
SHLIB_MAJOR=	9

.PATH:	${SRCDIR}/src/c++98
.PATH:	${SRCDIR}/src/c++11

CFLAGS+=	-I${.OBJDIR}
CXXFLAGS+=	-fno-implicit-templates
CXXFLAGS+=	-fdiagnostics-show-location=once
CXXFLAGS+=	-ffunction-sections -fdata-sections
LDFLAGS+=	-Wl,-z -Wl,relro -Wl,--gc-sections
VERSION_MAP=	${.CURDIR}/libstdc++-symbols.ver

FLAGS_GROUPS=		gnu11
gnu11_FLAGS=		-std=gnu++11
gnu11_FLAGS_FILES=	${cxx11_sources}

CONVARCS=	../components/libconv_supc/libsupc++convenience.a
CONVARCS+=	../components/libconv_1998/libc++98convenience.a
CONVARCS+=	../components/libconv_2011/libc++11convenience.a
LDADD=		-Wl,--whole-archive ${CONVARCS:.a=_pic.a} -lm
LDADD+=		-Wl,--no-whole-archive
ARADD=		${CONVARCS} ${LIBM}

SRCS=	${libstdc___la_SOURCES}

gstdint.h:
	echo "#include <stdint.h>" > ${.TARGET}

beforedepend:	gstdint.h
CLEANFILES=	gstdint.h

.include <bsd.lib.mk>
