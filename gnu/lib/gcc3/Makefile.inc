# $DragonFly: src/gnu/lib/gcc3/Attic/Makefile.inc,v 1.5 2004/02/02 23:35:07 dillon Exp $
#

.if !defined(GNU_LIB_GCC3_MAKEFILE_INC)
GNU_LIB_GCC3_MAKEFILE_INC=TRUE


GCCDIR= ${.CURDIR}/../../../../contrib/gcc-3.3
DFLYH= ${.CURDIR}/../../../usr.bin/cc3/config
CCTOOLS_SRC = ${.CURDIR}/../../../usr.bin/cc3/cc_tools
CCTOOLS_OBJ = ${.OBJDIR}/../../../usr.bin/cc3/cc_tools

USEGNUDIR=YES
CCVER=gcc3
.endif
