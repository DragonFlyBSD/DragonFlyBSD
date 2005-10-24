# $DragonFly: src/sys/dev/netif/nv/Attic/Makefile,v 1.3 2005/10/24 08:06:15 sephe Exp $

CONTRIBDIR=	${.CURDIR}/../../../contrib/dev/nv

KMOD=	if_nv
SRCS=	if_nv.c miibus_if.h device_if.h bus_if.h pci_if.h opt_polling.h
SRCS+=	adapter.h.patch basetype.h.patch os.h.patch phy.h.patch drvinfo.h.patch
OBJS+=	nvnetlib.o
KMODDEPS = miibus

.ifndef BUILDING_WITH_KERNEL
opt_polling.h:
	echo '#define DEVICE_POLLING 1' > ${.OBJDIR}/${.TARGET}
.endif

nvnetlib.o: ${CONTRIBDIR}/nvnetlib.o.uu
	uudecode < ${CONTRIBDIR}/nvnetlib.o.uu

.include <bsd.kmod.mk>
