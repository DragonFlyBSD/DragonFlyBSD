# $FreeBSD: head/sys/modules/sound/driver/maestro/Makefile 140867 2005-01-26 16:29:07Z imp $

.PATH: ${.CURDIR}/../../pci

KMOD=	snd_maestro
SRCS=	device_if.h bus_if.h pci_if.h
SRCS+=	maestro.c
WERROR=

.include <bsd.kmod.mk>
