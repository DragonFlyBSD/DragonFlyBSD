FWDIR=	${.CURDIR}/../../../../../contrib/dev/isp

.PATH: ${FWDIR}

KMOD=	isp_2200
FIRMWS=	isp_2200.fw:isp_2200:1

CLEANFILES=	isp_2200.fw

isp_2200.fw: ${FWDIR}/isp_2200.fw.uu
	uudecode -p ${FWDIR}/isp_2200.fw.uu > ${.TARGET}

.include <bsd.kmod.mk>
