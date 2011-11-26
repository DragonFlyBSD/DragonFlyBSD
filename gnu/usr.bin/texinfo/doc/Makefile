.include "../Makefile.inc"

INFO=		info info-stnd texinfo
SRCDIR=		${BASEDIR}/doc
INFOSECTION=	"System Utilities"

texinfo.texi:	texinfo.txi
	${LN} -fs ${.ALLSRC} ${.TARGET}
	
CLEANFILES=	texinfo.texi

.include <bsd.info.mk>
