# $FreeBSD: head/sys/modules/sound/driver/emu10k1/Makefile 230915 2012-02-02 17:25:39Z joel $

.PATH: ${.CURDIR}/../../../../dev/sound/pci

KMOD=	snd_emu10k1
SRCS=	device_if.h bus_if.h pci_if.h
SRCS+= mpufoi_if.h
SRCS+=	emu10k1.c

.include <bsd.kmod.mk>
