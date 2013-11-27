# $FreeBSD: src/sys/modules/stg/Makefile,v 1.1.2.2 2002/01/02 07:59:12 non Exp $

KMOD=	stg
SRCS=	tmc18c30.c tmc18c30_isa.c tmc18c30_pccard.c
SRCS+=	device_if.h bus_if.h pci_if.h isa_if.h card_if.h pccarddevs.h
SRCS+=	opt_cam.h opt_scsi.h opt_ddb.h

.include <bsd.kmod.mk>
