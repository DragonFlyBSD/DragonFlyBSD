# $DragonFly: src/gnu/usr.bin/cc40/cc_tools/genattrtab/Attic/Makefile,v 1.1 2005/06/05 22:43:20 corecode Exp $

.include "../Makefile.inc"
.include "../../Makefile.langs"

PROG=	genattrtab
SRCS=	genattrtab.c genautomata.c insn-modes.h gtype-desc.h insn-constants.h tm-preds.h options.h
SRCS+=	${RTL_SRCS} ${SUPPORT_SRCS} ${PRINT_SRCS} ${ERRORS_SRCS} ${VARRAY_SRCS}

LDADD+=	-lm

.include <bsd.hostprog.mk>
