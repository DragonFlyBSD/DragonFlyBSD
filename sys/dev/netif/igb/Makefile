KMOD=	if_igb
SRCS=	if_igb.c
SRCS+=	device_if.h bus_if.h pci_if.h
SRCS+=	opt_ifpoll.h opt_igb.h

.ifndef BUILDING_WITH_KERNEL
opt_ifpoll.h:
	touch ${.OBJDIR}/${.TARGET}
opt_igb.h:
	touch ${.OBJDIR}/${.TARGET}
.endif

.include <bsd.kmod.mk>
