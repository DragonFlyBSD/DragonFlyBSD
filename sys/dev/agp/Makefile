# $FreeBSD: src/sys/modules/agp/Makefile,v 1.2.2.4 2000/09/30 02:49:36 ps Exp $

KMOD	= agp
SRCS	= agp.c 
SRCS	+= agp_i810.c agp_ali.c agp_amd.c agp_x86_64.c agp_ati.c agp_nvidia.c
SRCS	+= agp_intel.c agp_sis.c agp_via.c
SRCS	+= agp_if.c device_if.h bus_if.h agp_if.h pci_if.h
SRCS	+= opt_pci.h
MFILES	= kern/device_if.m kern/bus_if.m dev/agp/agp_if.m bus/pci/pci_if.m

.include <bsd.kmod.mk>
