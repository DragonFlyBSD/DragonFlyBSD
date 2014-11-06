CCDIR=	${STOPDIR}

.include "../Makefile.inc"

.if ${CCVER:Mgcc*}
CFLAGS+=	-finhibit-size-directive
CFLAGS+=	-fno-toplevel-reorder
.endif

CFLAGS+=	-fno-inline-functions
CFLAGS+=	-fno-exceptions
CFLAGS+=	-fno-zero-initialized-in-bss
CFLAGS+=	-fno-tree-vectorize
CFLAGS+=	-fno-omit-frame-pointer
CFLAGS+=	-fno-asynchronous-unwind-tables
CFLAGS+=	-I.

.include "../../csu/Makefile.csu"
