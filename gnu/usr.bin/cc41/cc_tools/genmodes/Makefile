# $DragonFly: src/gnu/usr.bin/cc41/cc_tools/genmodes/Makefile,v 1.2 2007/08/25 15:29:29 corecode Exp $

.include "../Makefile.inc"

PROG=	genmodes
SRCS=	genmodes.c
SRCS+=	${ERRORS_SRCS}

all: ${TOOLGENDIR}/insn-modes.c ${TOOLGENDIR}/insn-modes.h ${TOOLGENDIR}/min-insn-modes.c

${TOOLGENDIR}/insn-modes.c: ${PROG}.nx
	${.OBJDIR}/${PROG}.nx > ${.TARGET}.tmp
	${MIC} ${.TARGET}.tmp ${.TARGET}

${TOOLGENDIR}/insn-modes.h: ${PROG}.nx
	${.OBJDIR}/${PROG}.nx -h > ${.TARGET}.tmp
	${MIC} ${.TARGET}.tmp ${.TARGET}

${TOOLGENDIR}/min-insn-modes.c: ${PROG}.nx
	${.OBJDIR}/${PROG}.nx -m > ${.TARGET}.tmp
	${MIC} ${.TARGET}.tmp ${.TARGET}

.include <bsd.hostprog.mk>
