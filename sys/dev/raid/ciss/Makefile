# $FreeBSD: src/sys/modules/ciss/Makefile,v 1.2 2002/10/27 12:09:51 mux Exp $

KMOD		=  ciss
SRCS		=  ciss.c
SRCS		+= opt_cam.h
SRCS		+= device_if.h bus_if.h pci_if.h

#KCFLAGS		+=-DCISS_DEBUG
#SRCS		+=opt_ddb.h

.include <bsd.kmod.mk>
