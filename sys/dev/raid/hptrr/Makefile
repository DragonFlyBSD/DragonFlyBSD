# $Id: Makefile.def,v 1.18 2007/03/22 11:20:15 gmm Exp $
# $FreeBSD: head/sys/modules/hptrr/Makefile 314651 2017-03-04 10:10:17Z ngie $

KMOD = hptrr
SRCS = opt_cam.h bus_if.h device_if.h pci_if.h
SRCS+= hptrr_os_bsd.c hptrr_osm_bsd.c hptrr_config.c
OBJS = hptrr_lib.bo

hptrr_lib.bo:	$(MACHINE_ARCH)-elf.hptrr_lib.o.uu
	uudecode -o ${.TARGET} < ${.ALLSRC}
	elfedit --output-osabi none ${.TARGET}

CLEANFILES+= hptrr_lib.bo

.include <bsd.kmod.mk>
