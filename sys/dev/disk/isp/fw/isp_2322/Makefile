.include <bsd.dragonfly.mk>

KMOD=	isp_2322
FIRMWS=	isp_2322.fw:isp_2322:1

CLEANFILES=	isp_2322.fw

isp_2322.fw: ${DF.SRCTOP}/sys/contrib/dev/isp/isp_2322.fw.uu
	${NORMAL_FW}

.include <bsd.kmod.mk>
