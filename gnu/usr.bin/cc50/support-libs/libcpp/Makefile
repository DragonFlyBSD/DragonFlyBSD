GCC_NO_PATH=	yes
GCC_NO_LIBS=	yes
LOCAL_CONFIG=	yes
.include "../Makefile.inc"
.PATH: ${GCCDIR}/libcpp

LIB=		cpp
INTERNALLIB=	YES

CFLAGS+=	-Duchar="unsigned char"

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

OBJS=		${CXX_BUILD:.c=.o}

# hack to force c++ compiler to compile *.c files to create library
.for cfile in ${CXX_BUILD}
${cfile:.c=.o}: ${cfile} localedir.h
	${CXX} ${STATIC_CXXFLAGS} ${CXXFLAGS} -c ${.IMPSRC} -o ${.TARGET}
.endfor

localedir.h:
	touch $@

CLEANFILES=	localedir.h

.include <bsd.lib.mk>
