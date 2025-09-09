# Despite the source files ending in ".c", the c++ compiler needs to build
# everything.  We have to roll our own targets to properly support this.

GCC_NO_PATH=	yes
LOCAL_CONFIG=	yes
.include "../Makefile.inc"
.PATH: ${GCCDIR}/libcpp

LIB=		cpp

CFLAGS+=	-I${.CURDIR}
CFLAGS+=	-Duchar="unsigned char"
CXXFLAGS+=	${NOEXCEPTION_FLAGS}
GOOD_CONFIG=	${.CURDIR}/../../support-libs/libcpp/config.h

CFLAGS+=	-DBOOTSTRAPPING

# From libcpp/Makefile.in (libcpp_a_SOURCES)
SRCS= \
	charset.cc directives.cc errors.cc \
	expr.cc files.cc identifiers.cc init.cc lex.cc line-map.cc macro.cc \
	mkdeps.cc pch.cc symtab.cc traditional.cc

localedir.h:
	touch $@

config.h: ${GOOD_CONFIG}
	cp ${.ALLSRC} .

beforedepend: localedir.h config.h
afterdepend: libcpp.na

CLEANFILES=	localedir.h config.h

.include "../../Makefile.intcxx_lib"
