SCRIPTS=	${.OBJDIR}/cc
SCRIPTSDIR=	/usr/libexec/custom
SYMLINKS=	cc ${SCRIPTSDIR}/cpp \
		cc ${SCRIPTSDIR}/c++ \
		cc ${SCRIPTSDIR}/gcc \
		cc ${SCRIPTSDIR}/g++
CLEANFILES=	cc
NOMAN=

${.OBJDIR}/cc: cc.sh
	sed "s#@@INCPREFIX@@#${USRDATA_PREFIX:S;^$;/;}#g" $> > $@

.include <bsd.prog.mk>
