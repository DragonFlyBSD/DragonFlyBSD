# $DragonFly: src/gnu/usr.bin/cc34/cc_tools/Makefile.inc,v 1.2 2004/12/20 18:10:11 dillon Exp $
#
# Portions of our build system need this file early, make sure it isn't
# included twice (because e.g. bsd.init.mk would also include it)
#
.if !target(__<cctools.Makefile.inc>__)
__<cctools.Makefile.inc>__:

TOP_PREFIX=	../

.include "../Makefile.inc"

CFLAGS+= -DHAVE_STDLIB_H -DHAVE_STRING_H -DGENERATOR_FILE
CFLAGS+= -I.

.include "Makefile.tools"

.endif
