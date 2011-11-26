KMOD    = if_iwi
SRCS    = if_iwi.c \
          device_if.h \
          bus_if.h \
          pci_if.h \
          opt_inet.h

SUBDIR	= fw

.if !defined(BUILDING_WITH_KERNEL)
opt_inet.h:
	echo "#define INET 1" > ${.TARGET}
.endif

.include <bsd.kmod.mk>
