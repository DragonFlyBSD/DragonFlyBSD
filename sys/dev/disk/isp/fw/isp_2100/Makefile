.include <bsd.dragonfly.mk>

KMOD=	isp_2100
FIRMWS=	isp_2100.fw:isp_2100:1

CLEANFILES=	isp_2100.fw

isp_2100.fw: ${DF.SRCTOP}/sys/contrib/dev/isp/isp_2100.fw.uu
	${NORMAL_FW}

.include <bsd.kmod.mk>
