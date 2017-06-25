FWDIR=	${.CURDIR}/../../../../../contrib/dev/isp

.PATH: ${FWDIR}

KMOD=	isp_2500
FIRMWS=	isp_2500.fw:isp_2500:1

CLEANFILES=	isp_2500.fw

isp_2500.fw: ${FWDIR}/isp_2500.fw.uu
	uudecode -p ${FWDIR}/isp_2500.fw.uu > ${.TARGET}

.include <bsd.kmod.mk>
