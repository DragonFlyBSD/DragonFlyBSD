# $DragonFly: src/gnu/usr.bin/cc41/cc_tools/genattrtab/Makefile,v 1.2 2007/08/25 15:29:28 corecode Exp $

.include "../Makefile.inc"

PROG=	genattrtab
SRCS=	genattrtab.c genautomata.c insn-modes.h gtype-desc.h insn-constants.h tm-preds.h options.h
SRCS+=	${RTL_SRCS} ${SUPPORT_SRCS} ${PRINT_SRCS} ${ERRORS_SRCS} ${VARRAY_SRCS}

LDADD+=	-lm

GENFILE= insn-attrtab.c
GENINPUT= ${MD_FILE}
.include "../Makefile.gen"

.include <bsd.hostprog.mk>
