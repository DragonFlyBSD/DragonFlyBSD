# Despite the .c extension, these files and headers must be built by c++

GCC_NO_LIBS=	yes
.include "../../../Makefile.inc"
.include "../../../Makefile.langs"

LIB=		guts-target
INTERNALLIB=

SRCS=		${C_TARGET_OBJS:T:.o=.c}

.include <bsd.lib.mk>

# hack to force c++ compiler to compile *.c files
MKDEPCC:=	${CXX}
CC:=		${CXX}
CFLAGS:=	${CXXFLAGS}
