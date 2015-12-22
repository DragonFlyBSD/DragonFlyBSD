# $FreeBSD: src/sys/modules/hpt27xx/Makefile,v 1.2 2012/02/21 21:20:52 dim Exp $

KMOD=	hpt27xx
SRCS=	bus_if.h device_if.h pci_if.h
SRCS+=	opt_cam.h opt_scsi.h
SRCS+=	hpt27xx_os_bsd.c hpt27xx_osm_bsd.c hpt27xx_config.c
OBJS+=	hpt27xx_lib.o

hpt27xx_lib.o:
	uudecode -p < ${.CURDIR}/$(MACHINE_ARCH)-elf.hpt27xx_lib.o.uu > ${.TARGET}

.include <bsd.kmod.mk>
