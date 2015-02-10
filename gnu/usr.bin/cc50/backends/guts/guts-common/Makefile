# Despite the .c extension, these files and headers must be built by c++

GCC_NO_LIBS=	yes
TOP_PREFIX=	../../

.include "../../../Makefile.inc"
.include "../../../Makefile.langs"

LIB=		guts-common
SRCS=		${C_COMMON_OBJS:T:.o=.c} main.c tree-browser.c

.include "..//../../Makefile.intcxx_lib"
