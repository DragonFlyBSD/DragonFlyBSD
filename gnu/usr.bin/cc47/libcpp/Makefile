GCC_NO_PATH=	# defined
LOCAL_CONFIG=	# defined
.include "../Makefile.inc"
.PATH: ${GCCDIR}/libcpp

LIB=		cpp
INTERNALLIB=	YES

CFLAGS+=	-Duchar="unsigned char"
CFLAGS+=	-DHWI_TYPE="${HWI_TYPE}"

SRCS=	charset.c \
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

#generated
SRCS+=	localedir.h

localedir.h:
	touch $@
	
CLEANFILES=	localedir.h

.include <bsd.lib.mk>
