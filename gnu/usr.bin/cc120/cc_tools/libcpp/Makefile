GCC_NO_PATH=	yes
LOCAL_CONFIG=	yes
.include "../Makefile.inc"
.PATH: ${GCCDIR}/libcpp

LIB=		cpp

CFLAGS+=	-DBOOTSTRAPPING
CFLAGS+=	-Duchar="unsigned char"
CXXFLAGS+=	${NOEXCEPTION_FLAGS}

# From libcpp/Makefile.in
libcpp_a_SOURCES = charset.cc directives.cc errors.cc \
	expr.cc files.cc identifiers.cc init.cc lex.cc line-map.cc macro.cc \
	mkdeps.cc pch.cc symtab.cc traditional.cc

SRCS=	${libcpp_a_SOURCES}

# Unused as NLS is disabled.
localedir.h:
	touch ${.TARGET}

config.h: ${.CURDIR}/../../support-libs/libcpp/config.h
	cp ${.ALLSRC} ${.TARGET}

beforedepend: localedir.h config.h
afterdepend: libcpp.na

CLEANFILES=	localedir.h config.h

.include <bsd.hostlib.mk>
