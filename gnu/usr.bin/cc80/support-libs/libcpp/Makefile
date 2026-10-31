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

CXX_BUILD=	charset.c \
		directives-only.c \
		directives.c \
		errors.c \
		expr.c \
		files.c \
		identifiers.c \
		init.c \
		lex.c \
		line-map.c \
		macro.c \
		mkdeps.c \
		pch.c \
		symtab.c \
		traditional.c

SRCS=		${CXX_BUILD}

# generated
SRCS+= localedir.h

localedir.h:
	touch $@

CLEANFILES=	localedir.h

.include <bsd.lib.mk>

# hack to force c++ compiler to compile *.c files
MKDEPCC:=	${CXX}
CC:=		${CXX}
CFLAGS:=	${CXXFLAGS}
