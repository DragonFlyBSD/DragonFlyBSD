# Despite the .c extension, these files and headers must be built by c++

GCC_NO_LIBS=	yes
TOP_PREFIX=	../../

.include "../../../Makefile.inc"
.include "../../../Makefile.langs"

LIB=		guts-target
INTERNALLIB=

GUTS_TARGETO=	${C_TARGET_OBJS:T}

OBJS=		${GUTS_TARGETO}

# hack to force c++ compiler to compile *.c files to create library
.for cfile in ${GUTS_TARGETO}
${cfile}: ${cfile:.o=.c}
	${CXX} ${STATIC_CXXFLAGS} ${CXXFLAGS} -c ${.IMPSRC} -o ${.TARGET}
.endfor

.include <bsd.lib.mk>
