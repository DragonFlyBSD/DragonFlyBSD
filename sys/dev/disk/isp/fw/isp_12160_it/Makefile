.include <bsd.dragonfly.mk>

KMOD=	isp_12160_it
FIRMWS=	isp_12160_it.fw:isp_12160_it:1

CLEANFILES=	isp_12160_it.fw

isp_12160_it.fw: ${DF.SRCTOP}/sys/contrib/dev/isp/isp_12160_it.fw.uu
	${NORMAL_FW}

.include <bsd.kmod.mk>
