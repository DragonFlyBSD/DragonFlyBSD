# $FreeBSD: head/sys/modules/mrsas/Makefile 265555 2014-05-07 16:16:49Z ambrisko $

KMOD=	mrsas
SRCS=	mrsas.c mrsas_cam.c mrsas_ioctl.c mrsas_fp.c
SRCS+=	device_if.h bus_if.h pci_if.h opt_cam.h opt_scsi.h

#CFLAGS+= -DMRSAS_DEBUG
.include <bsd.kmod.mk>
