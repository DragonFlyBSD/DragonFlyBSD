FWDIR=	${.CURDIR}/../../../../../contrib/dev/isp

.PATH: ${FWDIR}

KMOD=	isp_1080_it
FIRMWS=	isp_1080_it.fw:isp_1080_it:1

CLEANFILES=	isp_1080_it.fw

isp_1080_it.fw: ${FWDIR}/isp_1080_it.fw.uu
	uudecode -p ${FWDIR}/isp_1080_it.fw.uu > ${.TARGET}

.include <bsd.kmod.mk>
