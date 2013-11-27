KMOD	= if_re
SRCS	= if_re.c
SRCS	+= device_if.h bus_if.h pci_if.h pcidevs.h miibus_if.h
SRCS	+= opt_ifpoll.h

.ifndef BUILDING_WITH_KERNEL
opt_ifpoll.h:
	echo '#define IFPOLL_ENABLE 1' > ${.OBJDIR}/${.TARGET}
.endif

.include <bsd.kmod.mk>
