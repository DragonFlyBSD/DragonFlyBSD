FWDIR=	${.CURDIR}/../../../../../contrib/dev/mxge

.PATH: ${FWDIR}

KMOD=	mxge_rss_ethp_z8e
FIRMWS=	mxge_rss_ethp_z8e.fw:mxge_rss_ethp_z8e:544468

CLEANFILES=	mxge_rss_ethp_z8e.fw

mxge_rss_ethp_z8e.fw: ${FWDIR}/mxge_rss_ethp_z8e.fw.uu
	uudecode -p ${FWDIR}/mxge_rss_ethp_z8e.fw.uu > ${.TARGET}

.include <bsd.kmod.mk>
