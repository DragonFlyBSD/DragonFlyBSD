FWDIR=	${.CURDIR}/../../../../../contrib/dev/isp

.PATH: ${FWDIR}

KMOD=	isp_2400_multi
FIRMWS=	isp_2400_multi.fw:isp_2400_multi:1

CLEANFILES=	isp_2400_multi.fw

isp_2400_multi.fw: ${FWDIR}/isp_2400_multi.fw.uu
	uudecode -p ${FWDIR}/isp_2400_multi.fw.uu > ${.TARGET}

.include <bsd.kmod.mk>
