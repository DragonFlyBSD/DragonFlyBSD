# $FreeBSD: src/sys/modules/mly/Makefile,v 1.3 2001/07/14 00:12:23 msmith Exp $

KMOD=	mly
SRCS=	mly.c
SRCS+=	opt_scsi.h opt_cam.h
SRCS+=	device_if.h bus_if.h pci_if.h

#CFLAGS+=	-DMLY_DEBUG=3

.include <bsd.kmod.mk>
