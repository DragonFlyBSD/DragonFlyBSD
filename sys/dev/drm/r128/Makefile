# $FreeBSD: src/sys/modules/drm/r128/Makefile,v 1.4.2.1 2003/04/26 07:31:06 anholt Exp $
# $DragonFly: src/sys/dev/drm/r128/Makefile,v 1.2 2003/06/17 04:28:43 dillon Exp $

.PATH:	${.CURDIR}/../../../dev/drm
KMOD	= r128
NOMAN	= YES
SRCS	= r128_cce.c r128_drv.c r128_irq.c r128_state.c
SRCS	+=device_if.h bus_if.h pci_if.h opt_drm.h

.include <bsd.kmod.mk>
