FWDIR=	${.CURDIR}/../../../../../contrib/dev/isp

.PATH: ${FWDIR}

KMOD=	isp_1040
FIRMWS=	isp_1040.fw:isp_1040:1

CLEANFILES=	isp_1040.fw

isp_1040.fw: ${FWDIR}/isp_1040.fw.uu
	uudecode -p ${FWDIR}/isp_1040.fw.uu > ${.TARGET}

.include <bsd.kmod.mk>
