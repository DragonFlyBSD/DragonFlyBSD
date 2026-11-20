# $FreeBSD: src/sys/modules/wpifw/Makefile,v 1.2.2.1 2009/08/03 08:13:06 kensmith Exp $

.include <bsd.dragonfly.mk>

KMOD=	wpifw
FIRMWS=	iwlwifi-3945-15.32.2.9.fw:wpifw:153229

CLEANFILES=	iwlwifi-3945-15.32.2.9.fw

iwlwifi-3945-15.32.2.9.fw: ${DF.SRCTOP}/sys/contrib/dev/wpi/iwlwifi-3945-15.32.2.9.fw.uu
	${NORMAL_FW}

.include <bsd.kmod.mk>
