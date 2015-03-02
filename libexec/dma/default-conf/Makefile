FILESDIR=	/etc/dma
SHAREOWN=	root
SHAREGRP=	mail
FILESMODE=	640

GENFILES=	auth.conf.sample dma.conf.sample
CLEANFILES=	${GENFILES}
FILES+=		${GENFILES}

auth.conf.sample: ../auth.conf
	cp ${.ALLSRC} ${.TARGET}

dma.conf.sample: ../dma.conf
	cp ${.ALLSRC} ${.TARGET}

beforedepend: ${GENFILES}

.include <bsd.prog.mk>
