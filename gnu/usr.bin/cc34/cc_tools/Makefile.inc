# $DragonFly: src/gnu/usr.bin/cc34/cc_tools/Makefile.inc,v 1.1 2004/06/14 22:27:54 joerg Exp $

TOP_PREFIX=	../

.include "../Makefile.inc"

CFLAGS+= -DHAVE_STDLIB_H -DHAVE_STRING_H -DGENERATOR_FILE
CFLAGS+= -I.

.include "Makefile.tools"
