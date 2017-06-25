FWDIR=	${.CURDIR}/../../../../../contrib/dev/isp

.PATH: ${FWDIR}

KMOD=	isp_1040_it
FIRMWS=	isp_1040_it.fw:isp_1040_it:1

CLEANFILES=	isp_1040_it.fw

isp_1040_it.fw: ${FWDIR}/isp_1040_it.fw.uu
	uudecode -p ${FWDIR}/isp_1040_it.fw.uu > ${.TARGET}

.include <bsd.kmod.mk>
