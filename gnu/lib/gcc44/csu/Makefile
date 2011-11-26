CCDIR=	${STOPDIR}

.include "../Makefile.inc"

CFLAGS+=	-finhibit-size-directive
CFLAGS+=	-fno-inline-functions
CFLAGS+=	-fno-exceptions
CFLAGS+=	-fno-zero-initialized-in-bss
CFLAGS+=	-fno-toplevel-reorder
CFLAGS+=	-fno-tree-vectorize
CFLAGS+=	-fno-omit-frame-pointer
CFLAGS+=	-fno-asynchronous-unwind-tables
CFLAGS+=	-I.

.include "../../csu/Makefile.csu"
