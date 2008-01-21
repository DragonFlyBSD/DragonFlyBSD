# $FreeBSD: src/sys/modules/mly/Makefile,v 1.1.2.1 2000/10/27 10:07:02 msmith Exp $
# $DragonFly: src/sys/dev/raid/mly/Makefile,v 1.3 2008/01/21 02:27:37 pavalos Exp $

.PATH:		${.CURDIR}/../../dev/mly
KMOD		=  mly
SRCS		=  mly.c
SRCS		+= opt_scsi.h opt_cam.h
SRCS		+= device_if.h bus_if.h pci_if.h

CFLAGS+=	-DMLY_MODULE

.include <bsd.kmod.mk>
