.include "../Makefile.inc"

SRCDIR=		${BASEDIR}/doc
INFOSECTION=	"System Utilities"
INFOENTRY_diff=	"* DIFF: (diff).                 DIFF/PATCH Reference Manual."
INFO=		diffutils

diffutils.info: ${SRCDIR}/${.TARGET}
	cp ${.ALLSRC} ${.TARGET}

.include <bsd.info.mk>
