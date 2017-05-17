FWDIR=	${.CURDIR}/../../../../../contrib/dev/mxge

.PATH: ${FWDIR}

KMOD=	mxge_ethp_z8e
FIRMWS=	mxge_ethp_z8e.fw:mxge_ethp_z8e:387604

CLEANFILES=	mxge_ethp_z8e.fw

mxge_ethp_z8e.fw: ${FWDIR}/mxge_ethp_z8e.fw.uu
	uudecode -p ${FWDIR}/mxge_ethp_z8e.fw.uu > ${.TARGET}

.include <bsd.kmod.mk>
