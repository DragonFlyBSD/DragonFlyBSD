# $FreeBSD: src/sys/modules/agp/Makefile,v 1.2.2.4 2000/09/30 02:49:36 ps Exp $
# $DragonFly: src/sys/dev/agp/Makefile,v 1.4 2004/03/24 20:40:33 dillon Exp $

KMOD	= agp
SRCS	= agp.c 
SRCS	+= agp_i810.c agp_ali.c agp_amd.c agp_nvidia.c agp_intel.c
SRCS	+= agp_sis.c agp_via.c
SRCS	+= agp_if.c device_if.h bus_if.h agp_if.h pci_if.h
SRCS	+= opt_bdg.h opt_bus.h opt_pci.h
MFILES	= kern/device_if.m kern/bus_if.m dev/agp/agp_if.m bus/pci/pci_if.m

.include <bsd.kmod.mk>
