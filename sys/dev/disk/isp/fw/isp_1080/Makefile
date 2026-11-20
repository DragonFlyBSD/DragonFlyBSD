.include <bsd.dragonfly.mk>

KMOD=	isp_1080
FIRMWS=	isp_1080.fw:isp_1080:1

CLEANFILES=	isp_1080.fw

isp_1080.fw: ${DF.SRCTOP}/sys/contrib/dev/isp/isp_1080.fw.uu
	${NORMAL_FW}

.include <bsd.kmod.mk>
