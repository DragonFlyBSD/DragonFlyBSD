.include <bsd.dragonfly.mk>

KMOD=	isp_2300
FIRMWS=	isp_2300.fw:isp_2300:1

CLEANFILES=	isp_2300.fw

isp_2300.fw: ${DF.SRCTOP}/sys/contrib/dev/isp/isp_2300.fw.uu
	${NORMAL_FW}

.include <bsd.kmod.mk>
