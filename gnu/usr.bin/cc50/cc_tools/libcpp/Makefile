# Despite the source files ending in ".c", the c++ compiler needs to build
# everything.  We have to roll our own targets to properly support this.

GCC_NO_PATH=	yes
LOCAL_CONFIG=	yes
.include "../Makefile.inc"
.PATH: ${GCCDIR}/libcpp

LIB=		cpp

CFLAGS+=	-I${.CURDIR}
CFLAGS+=	-Duchar="unsigned char"
GOOD_CONFIG=	${.CURDIR}/../../support-libs/libcpp/config.h

SRCS=		charset.c \
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

localedir.h:
	touch $@

config.h: ${GOOD_CONFIG}
	cp ${.ALLSRC} .

beforedepend: localedir.h config.h
afterdepend: libcpp.na

CLEANFILES=	localedir.h config.h

.include "../../Makefile.intcxx_lib"
