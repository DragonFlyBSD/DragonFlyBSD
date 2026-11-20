.include <bsd.dragonfly.mk>

KMOD=	isp_12160
FIRMWS=	isp_12160.fw:isp_12160:1

CLEANFILES=	isp_12160.fw

isp_12160.fw: ${DF.SRCTOP}/sys/contrib/dev/isp/isp_12160.fw.uu
	${NORMAL_FW}

.include <bsd.kmod.mk>
