# Despite the .c extension, these files and headers must be built by c++

GCC_NO_LIBS=	yes
GCC_LANG_DIR=	gcc/cp
TOP_PREFIX=	../../

.include "../../../Makefile.inc"
.include "../../../Makefile.langs"

LIB=		guts-cxx

# note C_TARGET_OBJS == CXX_TARGET_OBJS
.for object in ${CXX_AND_OBJCXX_OBJS}
. if ! ${C_COMMON_OBJS:M${object}}
.  if ! ${C_TARGET_OBJS:M${object}}
SRCS+=		${object:T:.o=.c}
.  endif
. endif
.endfor

.include "../../../Makefile.intcxx_lib"
