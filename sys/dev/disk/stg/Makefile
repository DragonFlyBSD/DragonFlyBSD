# $FreeBSD: src/sys/modules/stg/Makefile,v 1.1.2.2 2002/01/02 07:59:12 non Exp $
# $DragonFly: src/sys/dev/disk/stg/Makefile,v 1.2 2003/06/17 04:28:47 dillon Exp $

.PATH: ${.CURDIR}/../../dev/stg

KMOD=	stg
SRCS=	tmc18c30.c tmc18c30_isa.c tmc18c30_pccard.c
SRCS+=	opt_bdg.h device_if.h bus_if.h pci_if.h isa_if.h card_if.h
SRCS+=	opt_cam.h opt_scsi.h opt_ddb.h
KMODDEPS=	scsi_low

.include <bsd.kmod.mk>
