# $DragonFly: src/gnu/usr.bin/cc40/cc_tools/Attic/Makefile.inc,v 1.1 2005/06/05 22:43:20 corecode Exp $
#
# Portions of our build system need this file early, make sure it isn't
# included twice (because e.g. bsd.init.mk would also include it)
#
.if !target(__<cctools.Makefile.inc>__)
__<cctools.Makefile.inc>__:

TOP_PREFIX=	../
NATIVE_TOOLS=	yes

.if !defined(GCC_NO_LIBERTY)
LDADD+=	../libiberty/libiberty.na
GCC_NO_LIBERTY=	yes
.endif

.include "../Makefile.inc"

CFLAGS+= -DHAVE_STDLIB_H -DHAVE_STRING_H -DGENERATOR_FILE
CFLAGS+= -I.

RTL_SRCS=	rtl.c read-rtl.c ggc-none.c min-insn-modes.c
SUPPORT_SRCS=	gensupport.c insn-conditions.c
EARLY_SRCS=	gensupport.c dummy-conditions.c
PRINT_SRCS=	print-rtl.c
ERRORS_SRCS=	errors.c
VARRAY_SRCS=	varray.c

.include "Makefile.tools"

.endif
