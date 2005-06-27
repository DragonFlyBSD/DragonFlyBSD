# $DragonFly: src/sys/dev/netif/iwi/Makefile,v 1.3 2005/06/27 11:28:54 corecode Exp $
#
KMOD    = if_iwi
SRCS    = if_iwi.c \
          device_if.h \
          bus_if.h \
          pci_if.h \
          opt_inet.h

#WARNS?=6

.if !defined(BUILDING_WITH_KERNEL)
opt_inet.h:
	echo "#define INET 1" > ${.TARGET}
.endif

.include <bsd.kmod.mk>
