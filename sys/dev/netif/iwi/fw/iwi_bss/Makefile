# $FreeBSD: src/sys/modules/iwifw/iwi_bss/Makefile,v 1.1 2007/03/02 11:42:55 flz Exp $

.include <bsd.dragonfly.mk>

KMOD=	iwi_bss
FIRMWS=	iwi_bss:iwi_bss:300
FIRMWARE_LICENSE=	intel_iwi

CLEANFILES=	iwi_bss

iwi_bss: ${DF.SRCTOP}/sys/contrib/dev/iwi/ipw2200-bss.fw.uu
	${NORMAL_FW}

.include <bsd.kmod.mk>

