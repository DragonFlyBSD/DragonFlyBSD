# $FreeBSD: src/sys/modules/rp/Makefile,v 1.3.2.1 2002/06/18 03:11:46 obrien Exp $
# $DragonFly: src/sys/dev/serial/rp/Makefile,v 1.2 2003/06/17 04:28:46 dillon Exp $

.PATH: ${.CURDIR}/../../dev/rp

KMOD=	rp
SRCS=	rp.c rp_pci.c device_if.h bus_if.h pci_if.h
CFLAGS+= -g

.include <bsd.kmod.mk>
