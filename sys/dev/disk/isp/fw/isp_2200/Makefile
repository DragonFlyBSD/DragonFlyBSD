.include <bsd.dragonfly.mk>

KMOD=	isp_2200
FIRMWS=	isp_2200.fw:isp_2200:1

CLEANFILES=	isp_2200.fw

isp_2200.fw: ${DF.SRCTOP}/sys/contrib/dev/isp/isp_2200.fw.uu
	${NORMAL_FW}

.include <bsd.kmod.mk>
