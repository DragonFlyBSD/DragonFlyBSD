FWDIR=	${.CURDIR}/../../../../../contrib/dev/mxge

.PATH: ${FWDIR}

KMOD=	mxge_rss_eth_z8e
FIRMWS=	mxge_rss_eth_z8e.fw:mxge_rss_eth_z8e:534724

CLEANFILES=	mxge_rss_eth_z8e.fw

mxge_rss_eth_z8e.fw: ${FWDIR}/mxge_rss_eth_z8e.fw.uu
	uudecode -p ${FWDIR}/mxge_rss_eth_z8e.fw.uu > ${.TARGET}

.include <bsd.kmod.mk>
