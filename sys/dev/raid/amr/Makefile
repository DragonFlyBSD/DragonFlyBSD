# $FreeBSD: src/sys/modules/amr/Makefile,v 1.14 2010/08/23 06:13:29 imp Exp $

SUBDIR= amr_cam
#.if ${MACHINE_ARCH} == "i386" || ${MACHINE_ARCH} == "x86_64"
#SUBDIR+= amr_linux
#.endif

KMOD=	amr
SRCS=	amr.c amr_pci.c amr_disk.c device_if.h bus_if.h pci_if.h
SRCS+=	opt_amr.h

# Enable a questionable optimisation for newer adapters
#CFLAGS+= -DAMR_QUARTZ_GOFASTER

.include <bsd.kmod.mk>
