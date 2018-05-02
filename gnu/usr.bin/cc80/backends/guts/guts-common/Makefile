# Despite the .c extension, these files and headers must be built by c++

GCC_NO_LIBS=	yes
TOP_PREFIX=	../../

.include "../../../Makefile.inc"
.include "../../../Makefile.langs"

LIB=		guts-common
INTERNALLIB=

GUTS_COMMONO=	${C_COMMON_OBJS:T} main.o
OBJS=		${GUTS_COMMONO}
SRCS=		${C_COMMON_SRCS}

# hack to force c++ compiler to compile *.c files to create library
.for cfile in ${GUTS_COMMONO}
${cfile}: ${cfile:.o=.c}
	${CXX} ${STATIC_CXXFLAGS} ${CXXFLAGS} -c ${.IMPSRC} -o ${.TARGET}
.endfor

.include <bsd.lib.mk>
