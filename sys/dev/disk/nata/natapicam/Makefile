.PATH:	${.CURDIR}/../../../../dev/disk/nata

KMOD=	natapicam
SRCS=	atapi-cam.c
SRCS+=	opt_ata.h opt_scsi.h opt_cam.h ata_if.h device_if.h bus_if.h

.include <bsd.kmod.mk>
