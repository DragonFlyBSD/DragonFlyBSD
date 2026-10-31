GCC_LANG_DIR=	gcc/cp
GCC_NO_LIBS=	yes
TOP_PREFIX=	../../

.include "../../../Makefile.inc"
.include "../../../Makefile.langs"

LIB=		guts-cxx
INTERNALLIB=	yes
WARNS=		1

excluded=	${C_COMMON_OBJS} ${CXX_TARGET_OBJS}
.for object in ${CXX_AND_OBJCXX_OBJS}
. if ! ${excluded:M${object}}
GUTS_OBJS+=	${object}
. endif
.endfor

SRCS=		${GUTS_OBJS:T:.o=.cc}

# derived from gcc/cp/Make-lang.in
FLAGS_GROUPS=		machine
machine_FLAGS_FILES=	module.cc
machine_FLAGS=		-DTARGET_MACHINE=\"${target_machine}\" \
			-DHOST_MACHINE=\"${target_machine}\"

.include <bsd.lib.mk>
