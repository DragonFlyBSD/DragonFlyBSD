FWDIR=	${.CURDIR}/../../../../../contrib/dev/isp

.PATH: ${FWDIR}

KMOD=	isp_2100
FIRMWS=	isp_2100.fw:isp_2100:1

CLEANFILES=	isp_2100.fw

isp_2100.fw: ${FWDIR}/isp_2100.fw.uu
	uudecode -p ${FWDIR}/isp_2100.fw.uu > ${.TARGET}

.include <bsd.kmod.mk>
