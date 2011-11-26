# $DragonFly: src/sys/dev/disk/nata/nataisa/Makefile,v 1.3 2006/12/10 23:39:43 tgen Exp $

.PATH:	${.CURDIR}/../../../../dev/disk/nata

KMOD=	nataisa
SRCS=	ata-isa.c
SRCS+=	opt_ata.h ata_if.h device_if.h bus_if.h isa_if.h

.include <bsd.kmod.mk>
