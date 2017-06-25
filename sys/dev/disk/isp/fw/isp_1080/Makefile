FWDIR=	${.CURDIR}/../../../../../contrib/dev/isp

.PATH: ${FWDIR}

KMOD=	isp_1080
FIRMWS=	isp_1080.fw:isp_1080:1

CLEANFILES=	isp_1080.fw

isp_1080.fw: ${FWDIR}/isp_1080.fw.uu
	uudecode -p ${FWDIR}/isp_1080.fw.uu > ${.TARGET}

.include <bsd.kmod.mk>
