FWDIR=	${.CURDIR}/../../../../../contrib/dev/isp

.PATH: ${FWDIR}

KMOD=	isp_2500_multi
FIRMWS=	isp_2500_multi.fw:isp_2500_multi:1

CLEANFILES=	isp_2500_multi.fw

isp_2500_multi.fw: ${FWDIR}/isp_2500_multi.fw.uu
	uudecode -p ${FWDIR}/isp_2500_multi.fw.uu > ${.TARGET}

.include <bsd.kmod.mk>
