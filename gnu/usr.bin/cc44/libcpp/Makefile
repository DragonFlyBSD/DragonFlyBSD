LIB=		cpp
INTERNALLIB=	YES

SRCS=	charset.c directives.c directives-only.c errors.c expr.c files.c \
	identifiers.c init.c lex.c line-map.c macro.c mkdeps.c \
	pch.c symtab.c traditional.c

SRCS+=	localedir.h
CLEANFILES=	localedir.h
localedir.h:
	touch $@

CFLAGS+= -I${.CURDIR} -I${.OBJDIR} -Duchar="unsigned char"

GCC_NO_PATH=	# defined

.include <bsd.lib.mk>

.PATH:	${GCCDIR}/libcpp
