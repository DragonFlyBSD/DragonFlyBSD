# $FreeBSD: src/sys/modules/aac/Makefile,v 1.9 2010/08/23 06:13:29 imp Exp $

KMOD=	aac
SRCS=	aac.c aac_pci.c aac_disk.c aac_cam.c
SRCS+=	opt_scsi.h opt_cam.h opt_aac.h
SRCS+=	device_if.h bus_if.h pci_if.h

# To enable debug output from the driver, uncomment these two lines.
#CFLAGS+= -DAAC_DEBUG=2
#SRCS+=	aac_debug.c

.include <bsd.kmod.mk>
