# $DragonFly: src/sys/dev/netif/iwi/Makefile,v 1.2 2005/06/27 10:58:22 corecode Exp $
#
KMOD    = if_iwi
SRCS    = if_iwi.c \
          device_if.h \
          bus_if.h \
          pci_if.h \
          opt_inet.h

CFLAGS	+=  -DIWI_DEBUG -DNBPFILTER=1

#WARNS?=6

opt_inet.h:
	echo "#define INET 1" > ${.TARGET}

.include <bsd.kmod.mk>
