FWDIR=	${.CURDIR}/../../../../../contrib/dev/isp

.PATH: ${FWDIR}

KMOD=	isp_12160
FIRMWS=	isp_12160.fw:isp_12160:1

CLEANFILES=	isp_12160.fw

isp_12160.fw: ${FWDIR}/isp_12160.fw.uu
	uudecode -p ${FWDIR}/isp_12160.fw.uu > ${.TARGET}

.include <bsd.kmod.mk>
