FWDIR=	${.CURDIR}/../../../../../contrib/dev/mxge

.PATH: ${FWDIR}

KMOD=	mxge_eth_z8e
FIRMWS=	mxge_eth_z8e.fw:mxge_eth_z8e:377284

CLEANFILES=	mxge_eth_z8e.fw

mxge_eth_z8e.fw: ${FWDIR}/mxge_eth_z8e.fw.uu
	uudecode -p ${FWDIR}/mxge_eth_z8e.fw.uu > ${.TARGET}

.include <bsd.kmod.mk>
