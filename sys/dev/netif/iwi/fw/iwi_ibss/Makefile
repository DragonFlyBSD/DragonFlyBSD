# $FreeBSD: src/sys/modules/iwifw/iwi_ibss/Makefile,v 1.1 2007/03/02 11:42:55 flz Exp $

.include <bsd.dragonfly.mk>

KMOD=	iwi_ibss
FIRMWS=	iwi_ibss:iwi_ibss:300
FIRMWARE_LICENSE=	intel_iwi

CLEANFILES=	iwi_ibss

iwi_ibss: ${DF.SRCTOP}/sys/contrib/dev/iwi/ipw2200-ibss.fw.uu
	${NORMAL_FW}

.include <bsd.kmod.mk>

