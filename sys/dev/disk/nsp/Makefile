# $FreeBSD: src/sys/modules/nsp/Makefile,v 1.1.2.2 2002/01/02 07:59:12 non Exp $
# $DragonFly: src/sys/dev/disk/nsp/Makefile,v 1.3 2006/06/25 11:02:38 corecode Exp $

.PATH: ${.CURDIR}/../../dev/nsp

KMOD=	nsp
SRCS=	nsp.c nsp_pccard.c
SRCS+=	device_if.h bus_if.h pci_if.h isa_if.h card_if.h
SRCS+=	opt_cam.h opt_scsi.h opt_ddb.h
KMODDEPS=	scsi_low

.include <bsd.kmod.mk>
