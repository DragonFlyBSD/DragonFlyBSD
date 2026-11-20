.include <bsd.dragonfly.mk>

KMOD=	mxge_ethp_z8e
FIRMWS=	mxge_ethp_z8e.fw:mxge_ethp_z8e:387604

CLEANFILES=	mxge_ethp_z8e.fw

mxge_ethp_z8e.fw: ${DF.SRCTOP}/sys/contrib/dev/mxge/mxge_ethp_z8e.fw.uu
	${NORMAL_FW}

.include <bsd.kmod.mk>
