.PATH:	${.CURDIR}/../../../../dev/disk/nata

KMOD=	natacard
SRCS=	ata-card.c
SRCS+=	opt_ata.h ata_if.h device_if.h bus_if.h isa_if.h card_if.h
SRCS+=	pccarddevs.h

.include <bsd.kmod.mk>
