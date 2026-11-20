.include <bsd.dragonfly.mk>

KMOD=	isp_2500
FIRMWS=	isp_2500.fw:isp_2500:1

CLEANFILES=	isp_2500.fw

isp_2500.fw: ${DF.SRCTOP}/sys/contrib/dev/isp/isp_2500.fw.uu
	${NORMAL_FW}

.include <bsd.kmod.mk>
