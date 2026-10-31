GCC_NO_LIBS=	yes
TOP_PREFIX=	../../

.include "../../../Makefile.inc"
.include "../../../Makefile.langs"

LIB=		guts-c
INTERNALLIB=	yes
WARNS=		1

excluded=	${C_COMMON_OBJS} ${C_TARGET_OBJS}
.for object in ${C_AND_OBJC_OBJS}
. if ! ${excluded:M${object}}
GUTS_OBJS+=	${object}
. endif
.endfor

SRCS=		${GUTS_OBJS:T:.o=.cc}

.include <bsd.lib.mk>
