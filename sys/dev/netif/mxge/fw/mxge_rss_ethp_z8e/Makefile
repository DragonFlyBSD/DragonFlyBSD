.include <bsd.dragonfly.mk>

KMOD=	mxge_rss_ethp_z8e
FIRMWS=	mxge_rss_ethp_z8e.fw:mxge_rss_ethp_z8e:544468

CLEANFILES=	mxge_rss_ethp_z8e.fw

mxge_rss_ethp_z8e.fw: ${DF.SRCTOP}/sys/contrib/dev/mxge/mxge_rss_ethp_z8e.fw.uu
	${NORMAL_FW}

.include <bsd.kmod.mk>
