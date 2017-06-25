FWDIR=	${.CURDIR}/../../../../../contrib/dev/isp

.PATH: ${FWDIR}

KMOD=	isp_2300
FIRMWS=	isp_2300.fw:isp_2300:1

CLEANFILES=	isp_2300.fw

isp_2300.fw: ${FWDIR}/isp_2300.fw.uu
	uudecode -p ${FWDIR}/isp_2300.fw.uu > ${.TARGET}

.include <bsd.kmod.mk>
