# $FreeBSD: src/sys/modules/iwifw/iwi_monitor/Makefile,v 1.1 2007/03/02 11:42:56 flz Exp $

.include <bsd.dragonfly.mk>

KMOD=	iwi_monitor
FIRMWS=	iwi_monitor:iwi_monitor:300
FIRMWARE_LICENSE=	intel_iwi

CLEANFILES=	iwi_monitor

iwi_monitor: ${DF.SRCTOP}/sys/contrib/dev/iwi/ipw2200-sniffer.fw.uu
	${NORMAL_FW}

.include <bsd.kmod.mk>

