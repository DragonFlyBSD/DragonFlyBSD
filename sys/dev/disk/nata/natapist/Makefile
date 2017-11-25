.PATH:	${.CURDIR}/../../../../dev/disk/nata

KMOD=	natapist
SRCS=	atapi-tape.c
SRCS+=	opt_ata.h ata_if.h device_if.h bus_if.h pci_if.h

.include <bsd.kmod.mk>
