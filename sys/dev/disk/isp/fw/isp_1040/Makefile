.include <bsd.dragonfly.mk>

KMOD=	isp_1040
FIRMWS=	isp_1040.fw:isp_1040:1

CLEANFILES=	isp_1040.fw

isp_1040.fw: ${DF.SRCTOP}/sys/contrib/dev/isp/isp_1040.fw.uu
	${NORMAL_FW}

.include <bsd.kmod.mk>
