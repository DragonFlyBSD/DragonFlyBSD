# $FreeBSD: src/sys/modules/mly/Makefile,v 1.1.2.1 2000/10/27 10:07:02 msmith Exp $
# $DragonFly: src/sys/dev/raid/pst/Makefile,v 1.1 2003/10/01 18:20:07 dillon Exp $

KMOD		=  pst
SRCS		=  pst-pci.c pst-iop.c pst-raid.c
SRCS		+= device_if.h bus_if.h pci_if.h

#CFLAGS+=	-DMLY_DEBUG=1

.include <bsd.kmod.mk>
