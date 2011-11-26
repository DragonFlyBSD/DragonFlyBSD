# $DragonFly: src/gnu/usr.bin/cc41/libcpp/Makefile,v 1.1 2006/09/27 12:10:35 corecode Exp $

LIB=		cpp
INTERNALLIB=	YES

SRCS=	charset.c directives.c errors.c expr.c files.c \
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
