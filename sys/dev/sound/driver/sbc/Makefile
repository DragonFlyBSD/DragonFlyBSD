# $FreeBSD: src/sys/modules/sound/driver/sbc/Makefile,v 1.1.2.3 2002/12/01 09:01:08 nyan Exp $
# $DragonFly: src/sys/dev/sound/driver/sbc/Makefile,v 1.2 2003/06/17 04:28:46 dillon Exp $

.PATH:		${.CURDIR}/../../../../dev/sound/isa
KMOD		= snd_sbc
SRCS		= device_if.h bus_if.h isa_if.h pci_if.h
SRCS		+= sbc.c

.include <bsd.kmod.mk>
