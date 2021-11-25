KMOD=	if_stge
SRCS=	if_stge.c
SRCS+=	opt_ifpoll.h
SRCS+=	miibus_if.h device_if.h bus_if.h pci_if.h

.ifndef KERNBUILDDIR
opt_ifpoll.h:
	echo '#define IFPOLL_ENABLE 1' > ${.OBJDIR}/${.TARGET}
.endif

.include <bsd.kmod.mk>
