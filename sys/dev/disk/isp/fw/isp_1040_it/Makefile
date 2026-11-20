.include <bsd.dragonfly.mk>

KMOD=	isp_1040_it
FIRMWS=	isp_1040_it.fw:isp_1040_it:1

CLEANFILES=	isp_1040_it.fw

isp_1040_it.fw: ${DF.SRCTOP}/sys/contrib/dev/isp/isp_1040_it.fw.uu
	${NORMAL_FW}

.include <bsd.kmod.mk>
