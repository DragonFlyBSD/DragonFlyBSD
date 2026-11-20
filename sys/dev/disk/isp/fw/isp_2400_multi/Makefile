.include <bsd.dragonfly.mk>

KMOD=	isp_2400_multi
FIRMWS=	isp_2400_multi.fw:isp_2400_multi:1

CLEANFILES=	isp_2400_multi.fw

isp_2400_multi.fw: ${DF.SRCTOP}/sys/contrib/dev/isp/isp_2400_multi.fw.uu
	${NORMAL_FW}

.include <bsd.kmod.mk>
