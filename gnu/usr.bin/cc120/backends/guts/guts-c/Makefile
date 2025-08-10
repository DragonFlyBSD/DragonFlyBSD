# Despite the .c extension, these files and headers must be built by c++

GCC_NO_LIBS=	yes
TOP_PREFIX=	../../

.include "../../../Makefile.inc"
.include "../../../Makefile.langs"

LIB=		guts-c
INTERNALLIB=

.for object in ${C_AND_OBJC_OBJS}
. if ! ${C_COMMON_OBJS:M${object}}
.  if ! ${C_TARGET_OBJS:M${object}}
GUTS_CO+=	${object:T}
.  endif
. endif
.endfor

OBJS=		${GUTS_CO}

.include <bsd.lib.mk>
