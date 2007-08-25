# $DragonFly: src/gnu/usr.bin/cc41/cc_tools/genextract/Makefile,v 1.2 2007/08/25 15:29:29 corecode Exp $

.include "../Makefile.inc"

PROG=	genextract
SRCS=	genextract.c insn-modes.h gtype-desc.h insn-constants.h tm-preds.h options.h
SRCS+=	insn-config.h
SRCS+=	${RTL_SRCS} ${SUPPORT_SRCS} ${PRINT_SRCS} ${ERRORS_SRCS}

GENFILE= insn-extract.c
GENINPUT= ${MD_FILE}
.include "../Makefile.gen"

.include <bsd.hostprog.mk>
