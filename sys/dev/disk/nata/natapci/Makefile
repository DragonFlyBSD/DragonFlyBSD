.PATH:	${.CURDIR}/../../../../dev/disk/nata

KMOD=	natapci
SRCS=	ata-pci.c ata-dma.c ata-chipset.c ata-sata.c
SRCS+=	opt_ata.h ata_if.h device_if.h bus_if.h pci_if.h

.include <bsd.kmod.mk>
