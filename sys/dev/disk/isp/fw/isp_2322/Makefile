FWDIR=	${.CURDIR}/../../../../../contrib/dev/isp

.PATH: ${FWDIR}

KMOD=	isp_2322
FIRMWS=	isp_2322.fw:isp_2322:1

CLEANFILES=	isp_2322.fw

isp_2322.fw: ${FWDIR}/isp_2322.fw.uu
	uudecode -p ${FWDIR}/isp_2322.fw.uu > ${.TARGET}

.include <bsd.kmod.mk>
