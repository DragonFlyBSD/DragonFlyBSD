# $DragonFly: src/sys/dev/disk/nata/nata/Makefile,v 1.2 2006/12/08 15:09:18 tgen Exp $

.PATH:	${.CURDIR}/../../../dev/disk/nata

KMOD=	nata
SRCS=	ata-all.c ata-queue.c ata-lowlevel.c ata_if.c
SRCS+=	opt_ata.h ata_if.h device_if.h bus_if.h

NOMAN=	

.include <bsd.kmod.mk>
