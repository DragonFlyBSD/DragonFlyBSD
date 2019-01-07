# $FreeBSD: head/sys/modules/mpr/Makefile 332122 2018-04-06 17:35:35Z brooks $

KMOD=	mpr
SRCS=	mpr_pci.c mpr.c mpr_sas.c mpr_table.c mpr_user.c
SRCS+=	mpr_config.c mpr_mapping.c mpr_sas_lsi.c
SRCS+=	opt_cam.h
SRCS+=	device_if.h bus_if.h pci_if.h

#KCFLAGS += -DMPR_DEBUG

.include <bsd.kmod.mk>
