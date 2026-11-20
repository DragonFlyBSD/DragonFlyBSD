.include <bsd.dragonfly.mk>

KMOD=	isp_2100_variant_1
FIRMWS=	isp_2100_variant_1.fw:isp_2100_variant_1:1

CLEANFILES=	isp_2100_variant_1.fw

isp_2100_variant_1.fw: ${DF.SRCTOP}/sys/contrib/dev/isp/isp_2100_variant_1.fw.uu
	${NORMAL_FW}

.include <bsd.kmod.mk>
