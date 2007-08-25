# $DragonFly: src/gnu/usr.bin/cc41/cc_tools/gengenrtl/Makefile,v 1.2 2007/08/25 15:29:29 corecode Exp $

.include "../Makefile.inc"

PROG=	gengenrtl
SRCS=	gengenrtl.c
SRCS+=	${ERRORS_SRCS}

all: ${TOOLGENDIR}/genrtl.c ${TOOLGENDIR}/genrtl.h

${TOOLGENDIR}/genrtl.c: ${PROG}.nx
	${.OBJDIR}/${PROG}.nx > ${.TARGET}.tmp
	${MIC} ${.TARGET}.tmp ${.TARGET}

${TOOLGENDIR}/genrtl.h: ${PROG}.nx
	${.OBJDIR}/${PROG}.nx -h > ${.TARGET}.tmp
	${MIC} ${.TARGET}.tmp ${.TARGET}

.include <bsd.hostprog.mk>
