# $FreeBSD: src/sys/modules/mps/Makefile,v 1.3 2010/10/17 20:01:56 scottl Exp $

KMOD=	mps
SRCS=	mps_pci.c mps.c mps_sas.c mps_table.c mps_user.c
SRCS+=	opt_mps.h opt_cam.h opt_compat.h
SRCS+=	device_if.h bus_if.h pci_if.h

#CFLAGS += -DMPS_DEBUG

.include <bsd.kmod.mk>
