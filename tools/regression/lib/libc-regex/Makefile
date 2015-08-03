PROG=		testregex
NOMAN=		yes

LOCALE?=	C
TYPES=		basic categorize nullsubexpr leftassoc rightassoc \
		forcedassoc repetition

.for type in ${TYPES}
test-${type}: testregex datafiles/${type}.dat
	@echo
	@echo Testing ${type} ...
	@env LANG=${LOCALE} \
		${.CURDIR}/testregex < ${.CURDIR}/datafiles/${type}.dat
.endfor

full-test-run: ${TYPES:C/^/test-/}

.include <bsd.prog.mk>
