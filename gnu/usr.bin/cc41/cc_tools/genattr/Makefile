# $DragonFly: src/gnu/usr.bin/cc41/cc_tools/genattr/Makefile,v 1.2 2007/08/25 15:29:28 corecode Exp $

.include "../Makefile.inc"

PROG=	genattr
SRCS=	genattr.c insn-modes.h gtype-desc.h insn-constants.h tm-preds.h options.h
SRCS+=	${RTL_SRCS} ${SUPPORT_SRCS} ${PRINT_SRCS} ${ERRORS_SRCS}

GENFILE= insn-attr.h
GENINPUT= ${MD_FILE}
.include "../Makefile.gen"

.include <bsd.hostprog.mk>
