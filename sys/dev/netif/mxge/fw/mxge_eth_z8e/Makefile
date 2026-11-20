.include <bsd.dragonfly.mk>

KMOD=	mxge_eth_z8e
FIRMWS=	mxge_eth_z8e.fw:mxge_eth_z8e:377284

CLEANFILES=	mxge_eth_z8e.fw

mxge_eth_z8e.fw: ${DF.SRCTOP}/sys/contrib/dev/mxge/mxge_eth_z8e.fw.uu
	${NORMAL_FW}

.include <bsd.kmod.mk>
