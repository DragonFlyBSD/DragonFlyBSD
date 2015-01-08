# $FreeBSD: head/sys/modules/sound/driver/hdspe/Makefile 232337 2012-03-01 13:10:18Z mav $

.PATH: ${.CURDIR}/../../../../dev/sound/pci

KMOD=	snd_hdspe
SRCS=	device_if.h bus_if.h pci_if.h
SRCS+=	hdspe.c hdspe-pcm.c hdspe.h

.include <bsd.kmod.mk>
