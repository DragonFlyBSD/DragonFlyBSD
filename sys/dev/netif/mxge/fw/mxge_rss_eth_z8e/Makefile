.include <bsd.dragonfly.mk>

KMOD=	mxge_rss_eth_z8e
FIRMWS=	mxge_rss_eth_z8e.fw:mxge_rss_eth_z8e:534724

CLEANFILES=	mxge_rss_eth_z8e.fw

mxge_rss_eth_z8e.fw: ${DF.SRCTOP}/sys/contrib/dev/mxge/mxge_rss_eth_z8e.fw.uu
	${NORMAL_FW}

.include <bsd.kmod.mk>
