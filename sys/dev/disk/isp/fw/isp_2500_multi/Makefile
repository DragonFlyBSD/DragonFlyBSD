.include <bsd.dragonfly.mk>

KMOD=	isp_2500_multi
FIRMWS=	isp_2500_multi.fw:isp_2500_multi:1

CLEANFILES=	isp_2500_multi.fw

isp_2500_multi.fw: ${DF.SRCTOP}/sys/contrib/dev/isp/isp_2500_multi.fw.uu
	${NORMAL_FW}

.include <bsd.kmod.mk>
