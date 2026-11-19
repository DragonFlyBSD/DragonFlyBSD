GCC_NO_LIBS=	yes
.include "../../../Makefile.inc"
.include "../../../Makefile.langs"

LIB=		guts-target
INTERNALLIB=	yes

# NOTE: ${C_TARGET_OBJS} == ${CXX_TARGET_OBJS}, so this library is used
# by both guts-c and guts-cxx libraries.
.if ${C_TARGET_OBJS:O} != ${CXX_TARGET_OBJS:O}
.error unexpected C_TARGET_OBJS != CXX_TARGET_OBJS
.endif

SRCS=		${C_TARGET_OBJS:T:.o=.cc}

.include <bsd.lib.mk>
