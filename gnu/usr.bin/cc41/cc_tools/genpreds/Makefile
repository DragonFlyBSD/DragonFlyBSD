# $DragonFly: src/gnu/usr.bin/cc41/cc_tools/genpreds/Makefile,v 1.2 2007/08/25 15:29:29 corecode Exp $

.include "../Makefile.inc"

PROG=	genpreds
SRCS=	genpreds.c insn-modes.h gtype-desc.h
SRCS+=	${RTL_SRCS} ${EARLY_SRCS} ${PRINT_SRCS} ${ERRORS_SRCS}

all: ${TOOLGENDIR}/insn-preds.c ${TOOLGENDIR}/tm-preds.h

${TOOLGENDIR}/insn-preds.c: ${PROG}.nx ${MD_FILE}
	${.OBJDIR}/${PROG}.nx ${MD_FILE} > ${.TARGET}.tmp
	${MIC} ${.TARGET}.tmp ${.TARGET}

${TOOLGENDIR}/tm-preds.h: ${PROG}.nx ${MD_FILE}
	${.OBJDIR}/${PROG}.nx ${MD_FILE} -h > ${.TARGET}.tmp
	${MIC} ${.TARGET}.tmp ${.TARGET}

.include <bsd.hostprog.mk>
