GCC_NO_PATH=	yes
GCC_NO_LIBS=	yes
LOCAL_CONFIG=	yes
.include "../Makefile.inc"
.PATH: ${GCCDIR}/libcpp

LIB=		cpp
INTERNALLIB=	YES

CFLAGS+=	-Duchar="unsigned char"
CXXFLAGS+=	${NOEXCEPTION_FLAGS}

.if defined(BOOTSTRAPPING)
CFLAGS+=	-DBOOTSTRAPPING
.endif

# From libcpp/Makefile.in (libcpp_a_OBJS)
OBJS= charset.o directives.o errors.o \
	expr.o files.o identifiers.o init.o lex.o line-map.o macro.o \
	mkdeps.o pch.o symtab.o traditional.o

${OBJS}: localedir.h

localedir.h:
	touch $@

CLEANFILES=	localedir.h

.include <bsd.lib.mk>
