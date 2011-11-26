# $DragonFly: src/sys/dev/disk/nata/natacard/Makefile,v 1.3 2006/12/10 23:39:42 tgen Exp $

.PATH:	${.CURDIR}/../../../../dev/disk/nata

KMOD=	natacard
SRCS=	ata-card.c
SRCS+=	opt_ata.h ata_if.h device_if.h bus_if.h isa_if.h card_if.h

.include <bsd.kmod.mk>
