# $DragonFly: src/sys/dev/netif/nv/Attic/Makefile,v 1.1 2004/08/28 15:08:02 joerg Exp $

CONTRIBDIR=	${.CURDIR}/../../../contrib/dev/nv

KMOD=	if_nv
SRCS=	if_nv.c miibus_if.h device_if.h bus_if.h pci_if.h
SRCS+=	adapter.h.patch basetype.h.patch os.h.patch phy.h.patch
OBJS+=	nvnetlib.o
KMODDEPS = miibus

nvnetlib.o: ${CONTRIBDIR}/nvnetlib.o.uu
	uudecode < ${CONTRIBDIR}/nvnetlib.o.uu

.include <bsd.kmod.mk>
