SUBDIR=	dotdot openat

.include <bsd.subdir.mk>

TESTDIR?= /tmp/${.CURDIR:T}

test:
	@echo "Test directory: ${TESTDIR}"
	[ ! -d "${TESTDIR}" ] || rm -rf ${TESTDIR}
	cpdup -i0 ${.CURDIR} ${TESTDIR}
	cd ${TESTDIR} && dfregress dfregress.run

	dfr2text ${TESTDIR}/dfregress.plist
