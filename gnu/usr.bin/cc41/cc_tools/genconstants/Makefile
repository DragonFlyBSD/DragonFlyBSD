# $DragonFly: src/gnu/usr.bin/cc41/cc_tools/genconstants/Makefile,v 1.2 2007/08/25 15:29:29 corecode Exp $

.include "../Makefile.inc"

PROG=	genconstants
SRCS=	genconstants.c insn-modes.h gtype-desc.h
SRCS+=	${RTL_SRCS} ${EARLY_SRCS} ${ERRORS_SRCS}

GENFILE= insn-constants.h
GENINPUT= ${MD_FILE}
.include "../Makefile.gen"

.include <bsd.hostprog.mk>
