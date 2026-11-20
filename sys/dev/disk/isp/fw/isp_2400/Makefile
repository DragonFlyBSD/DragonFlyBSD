.include <bsd.dragonfly.mk>

KMOD=	isp_2400
FIRMWS=	isp_2400.fw:isp_2400:1

CLEANFILES=	isp_2400.fw

isp_2400.fw: ${DF.SRCTOP}/sys/contrib/dev/isp/isp_2400.fw.uu
	${NORMAL_FW}

.include <bsd.kmod.mk>
