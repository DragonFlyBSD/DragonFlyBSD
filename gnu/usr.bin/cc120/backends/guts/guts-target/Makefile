# Despite the .c extension, these files and headers must be built by c++

GCC_NO_LIBS=	yes
TOP_PREFIX=	../../

.include "../../../Makefile.inc"
.include "../../../Makefile.langs"

LIB=		guts-target
INTERNALLIB=

GUTS_TARGETO=	${C_TARGET_OBJS:T}

OBJS=		${GUTS_TARGETO}

.include <bsd.lib.mk>
