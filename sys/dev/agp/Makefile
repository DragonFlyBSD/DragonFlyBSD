KMOD	= agp
SRCS	= agp.c 
SRCS	+= agp_i810.c agp_amd64.c agp_sis.c agp_via.c
SRCS	+= agp_if.c device_if.h bus_if.h agp_if.h pci_if.h pcidevs.h
SRCS	+= opt_agp.h

CFLAGS+= -I${SYSDIR}/dev/drm/include

.include <bsd.kmod.mk>
