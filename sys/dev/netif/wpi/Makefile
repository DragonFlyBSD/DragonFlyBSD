KMOD	= if_wpi
SRCS	= if_wpi.c device_if.h bus_if.h pci_if.h opt_wlan.h opt_wpi.h
SRCS	+= miibus_if.h

SUBDIR	= fw

.ifndef BUILDING_WITH_KERNEL

opt_wpi.h:
	touch ${.OBJDIR}/${.TARGET}

.endif

.include <bsd.kmod.mk>
