GCC_NO_PATH=	yes
GCC_NO_LIBS=	yes
LOCAL_CONFIG=	yes
.include "../Makefile.inc"
.PATH: ${GCCDIR}/libcpp

LIB=		cpp
INTERNALLIB=	YES
WARNS=		1

CFLAGS+=	-Duchar="unsigned char"
CXXFLAGS+=	${NOEXCEPTION_FLAGS}

.if defined(BOOTSTRAPPING)
CFLAGS+=	-DBOOTSTRAPPING
.endif

# From libcpp/Makefile.in
libcpp_a_SOURCES = charset.cc directives.cc errors.cc \
	expr.cc files.cc identifiers.cc init.cc lex.cc line-map.cc macro.cc \
	mkdeps.cc pch.cc symtab.cc traditional.cc

SRCS=	${libcpp_a_SOURCES}

# generated
SRCS+= localedir.h

localedir.h:
	touch ${.TARGET}

CLEANFILES=	localedir.h

.include <bsd.lib.mk>
