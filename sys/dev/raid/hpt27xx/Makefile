# $FreeBSD: src/sys/modules/hpt27xx/Makefile,v 1.2 2012/02/21 21:20:52 dim Exp $

KMOD=	hpt27xx
SRCS=	bus_if.h device_if.h pci_if.h
SRCS+=	opt_cam.h
SRCS+=	hpt27xx_os_bsd.c hpt27xx_osm_bsd.c hpt27xx_config.c
OBJS+=	hpt27xx_lib.bo

hpt27xx_lib.bo:	$(MACHINE_ARCH)-elf.hpt27xx_lib.o.uu
	uudecode -o ${.TARGET} < ${.ALLSRC}
	elfedit --output-osabi none ${.TARGET}

CLEANFILES+= hpt27xx_lib.bo

.include <bsd.kmod.mk>
