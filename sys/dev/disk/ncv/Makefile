# $FreeBSD: src/sys/modules/ncv/Makefile,v 1.1.2.2 2002/01/02 07:59:12 non Exp $

KMOD=	ncv
SRCS=	ncr53c500_pccard.c ncr53c500.c 
SRCS+=	device_if.h bus_if.h pci_if.h isa_if.h card_if.h pccarddevs.h
SRCS+=	opt_cam.h opt_scsi.h opt_ddb.h

.include <bsd.kmod.mk>
