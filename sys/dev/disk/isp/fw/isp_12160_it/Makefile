FWDIR=	${.CURDIR}/../../../../../contrib/dev/isp

.PATH: ${FWDIR}

KMOD=	isp_12160_it
FIRMWS=	isp_12160_it.fw:isp_12160_it:1

CLEANFILES=	isp_12160_it.fw

isp_12160_it.fw: ${FWDIR}/isp_12160_it.fw.uu
	uudecode -p ${FWDIR}/isp_12160_it.fw.uu > ${.TARGET}

.include <bsd.kmod.mk>
