SCRIPTS=	${.OBJDIR}/cc
SCRIPTSDIR=	/usr/libexec/custom
SYMLINKS=	cc ${SCRIPTSDIR}/cpp \
		cc ${SCRIPTSDIR}/c++ \
		cc ${SCRIPTSDIR}/gcc \
		cc ${SCRIPTSDIR}/g++
CLEANFILES=	cc
NOMAN=

${.OBJDIR}/cc: cc.sh
	sed "s#@@PREFIX2@@#${USRDATA_PREFIX}/usr#g" $> > $@

.include <bsd.prog.mk>
