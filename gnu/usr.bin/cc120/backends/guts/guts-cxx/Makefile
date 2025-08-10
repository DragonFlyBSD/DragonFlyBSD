# Despite the .c extension, these files and headers must be built by c++

GCC_NO_LIBS=	yes
GCC_LANG_DIR=	gcc/cp
TOP_PREFIX=	../../

.include "../../../Makefile.inc"
.include "../../../Makefile.langs"

CFLAGS+=	-DTARGET_MACHINE=\"${target_machine}\" -DHOST_MACHINE=\"${target_machine}\"
LIB=		guts-cxx
INTERNALLIB=

# note C_TARGET_OBJS == CXX_TARGET_OBJS
.for object in ${CXX_OBJS}
. if ! ${C_COMMON_OBJS:M${object}}
.  if ! ${C_TARGET_OBJS:M${object}}
GUTS_CXXO+=	${object:T}
.  endif
. endif
.endfor

OBJS=		${GUTS_CXXO}
SRCS=		${CXX_COMMON_SRCS}

.include <bsd.lib.mk>
