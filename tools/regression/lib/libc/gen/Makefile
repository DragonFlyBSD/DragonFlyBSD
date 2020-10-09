# $FreeBSD: src/tools/regression/lib/libc/gen/Makefile,v 1.11 2013/05/21 00:28:11 svnexp Exp $

TESTS=	test-wordexp

.PHONY: tests
tests: ${TESTS}
	for p in ${TESTS}; do ${.OBJDIR}/$$p; done

.PHONY: clean
clean:
	-rm -f ${TESTS}
