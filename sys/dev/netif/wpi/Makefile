KMOD	= if_wpi
SRCS	= if_wpi.c device_if.h bus_if.h pci_if.h
SRCS	+= opt_polling.h
SRCS	+= miibus_if.h

SUBDIR	= fw

.ifndef BUILDING_WITH_KERNEL
opt_polling.h:
	echo '#define DEVICE_POLLING 1' > ${.OBJDIR}/${.TARGET}
.endif

.include <bsd.kmod.mk>
