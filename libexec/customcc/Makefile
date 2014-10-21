SCRIPTS=	${.OBJDIR}/cc
SCRIPTSDIR=	/usr/libexec/custom
SYMLINKS=	cc ${SCRIPTSDIR}/cpp \
		cc ${SCRIPTSDIR}/c++ \
		cc ${SCRIPTSDIR}/gcc \
		cc ${SCRIPTSDIR}/g++ \
		cc ${SCRIPTSDIR}/clang-cpp \
		cc ${SCRIPTSDIR}/clang++ \
		cc ${SCRIPTSDIR}/clang \
		cc ${SCRIPTSDIR}/gcov \
		cc ${SCRIPTSDIR}/CC
CLEANFILES=	cc
NOMAN=

DFLYVERSION!=	awk '/^\#define[[:blank:]]__DragonFly_version/ {print $$3}' < ${.CURDIR}/../../sys/sys/param.h
MACHREL!=	echo ${DFLYVERSION} | awk '{a=int($$1/100000); b=int(($$1-(a*100000))/100); print a "." b}'

${.OBJDIR}/cc: cc.sh
	sed -e "s#@@INCPREFIX@@#${USRDATA_PREFIX:S;^$;/;}#g" \
		-e "s|@@MACHARCH@@|${MACHINE_ARCH}|g" \
		-e "s|@@MACHREL@@|${MACHREL}|g" \
		$> > $@

.include <bsd.prog.mk>
