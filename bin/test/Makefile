#        @(#)Makefile	8.1 (Berkeley) 5/31/93
# $FreeBSD: head/bin/test/Makefile 322515 2017-08-14 19:21:37Z ngie $

PROG=	test

LINKS=	${BINDIR}/test ${BINDIR}/[
MLINKS=	test.1 [.1

regress: test
	cd ${.CURDIR}/../../tools/regression/bin/test && \
	${MAKE} TEST=${.OBJDIR}/test

.include <bsd.prog.mk>
