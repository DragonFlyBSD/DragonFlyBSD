SUBDIR=	h_io_assist h_mem_assist

.include <bsd.subdir.mk>

TESTDIR?= /tmp/${.CURDIR:T}

test:
	kldstat -qm nvmm || kldload -n nvmm

	@echo "Test directory: ${TESTDIR}"
	[ ! -d "${TESTDIR}" ] || rm -rf ${TESTDIR}
	cpdup -i0 ${.CURDIR} ${TESTDIR}
	cd ${TESTDIR} && dfregress dfregress.run

	dfr2text ${TESTDIR}/dfregress.plist
