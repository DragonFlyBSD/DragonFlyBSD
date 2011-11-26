# $DragonFly: src/gnu/usr.bin/cc41/cc_tools/gcov-iov/Makefile,v 1.2 2007/08/25 15:29:28 corecode Exp $

.include "../Makefile.inc"

PROG=	gcov-iov

all: ${TOOLGENDIR}/gcov-iov.h
${TOOLGENDIR}/gcov-iov.h: BASE-VER DEV-PHASE ${PROG}.nx
	${.OBJDIR}/${PROG}.nx "`cat ${.ALLSRC:M*BASE-VER}`" \
		"`cat ${.ALLSRC:M*DEV-PHASE}`" > ${.TARGET}.tmp
	${MIC} ${.TARGET}.tmp ${.TARGET}

.include <bsd.hostprog.mk>
