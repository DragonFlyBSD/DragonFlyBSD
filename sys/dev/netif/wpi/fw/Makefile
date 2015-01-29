# $FreeBSD: src/sys/modules/wpifw/Makefile,v 1.2.2.1 2009/08/03 08:13:06 kensmith Exp $

FWDIR=	${.CURDIR}/../../../../contrib/dev/wpi

.PATH: ${FWDIR}

KMOD=	wpifw
FIRMWS=	iwlwifi-3945-15.32.2.9.fw:wpifw:153229

CLEANFILES=	iwlwifi-3945-15.32.2.9.fw

iwlwifi-3945-15.32.2.9.fw: ${FWDIR}/iwlwifi-3945-15.32.2.9.fw.uu
	uudecode -p ${FWDIR}/iwlwifi-3945-15.32.2.9.fw.uu > ${.TARGET}

.include <bsd.kmod.mk>
