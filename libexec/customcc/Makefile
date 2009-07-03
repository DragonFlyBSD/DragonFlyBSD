SCRIPTS=	cc
SCRIPTSDIR=	/usr/libexec/custom
SYMLINKS=	cc ${SCRIPTSDIR}/cpp \
		cc ${SCRIPTSDIR}/c++ \
		cc ${SCRIPTSDIR}/gcc \
		cc ${SCRIPTSDIR}/g++
CLEANFILES=	cc
NOMAN=

beforeinstall:
	sed "s#@@PREFIX2@@#${USRDATA_PREFIX}/usr#g" ${.CURDIR}/cc.sh >cc

.include <bsd.prog.mk>
