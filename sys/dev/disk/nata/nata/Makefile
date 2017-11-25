.PATH:	${.CURDIR}/../../../../dev/disk/nata

KMOD=	nata
SRCS=	ata-all.c ata-queue.c ata-lowlevel.c ata_if.c
SRCS+=	opt_ata.h ata_if.h device_if.h bus_if.h

.include <bsd.kmod.mk>
