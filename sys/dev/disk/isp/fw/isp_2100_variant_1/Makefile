FWDIR=	${.CURDIR}/../../../../../contrib/dev/isp

.PATH: ${FWDIR}

KMOD=	isp_2100_variant_1
FIRMWS=	isp_2100_variant_1.fw:isp_2100_variant_1:1

CLEANFILES=	isp_2100_variant_1.fw

isp_2100_variant_1.fw: ${FWDIR}/isp_2100_variant_1.fw.uu
	uudecode -p ${FWDIR}/isp_2100_variant_1.fw.uu > ${.TARGET}

.include <bsd.kmod.mk>
