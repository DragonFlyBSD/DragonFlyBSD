# $DragonFly: src/gnu/usr.bin/cc41/cc_tools/genconditions/Makefile,v 1.2 2007/08/25 15:29:29 corecode Exp $

.include "../Makefile.inc"

PROG=	genconditions
SRCS=	genconditions.c insn-modes.h gtype-desc.h
SRCS+=	${EARLY_SRCS} ${RTL_SRCS} ${ERRORS_SRCS}

GENFILE= insn-conditions.c
GENINPUT= ${MD_FILE}
.include "../Makefile.gen"

.include <bsd.hostprog.mk>
