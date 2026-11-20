.include <bsd.dragonfly.mk>

KMOD=	isp_1080_it
FIRMWS=	isp_1080_it.fw:isp_1080_it:1

CLEANFILES=	isp_1080_it.fw

isp_1080_it.fw: ${DF.SRCTOP}/sys/contrib/dev/isp/isp_1080_it.fw.uu
	${NORMAL_FW}

.include <bsd.kmod.mk>
