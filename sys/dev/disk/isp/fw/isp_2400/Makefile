FWDIR=	${.CURDIR}/../../../../../contrib/dev/isp

.PATH: ${FWDIR}

KMOD=	isp_2400
FIRMWS=	isp_2400.fw:isp_2400:1

CLEANFILES=	isp_2400.fw

isp_2400.fw: ${FWDIR}/isp_2400.fw.uu
	uudecode -p ${FWDIR}/isp_2400.fw.uu > ${.TARGET}

.include <bsd.kmod.mk>
