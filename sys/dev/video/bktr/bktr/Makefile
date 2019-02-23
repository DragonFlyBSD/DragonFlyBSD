# Makefile for the bktr Brooktree Bt848/Bt878 PCI video capture device

# $FreeBSD: src/sys/modules/bktr/bktr/Makefile,v 1.1.2.3 2001/07/24 09:51:06 dd Exp $

.PATH:  ${.CURDIR}/..
KMOD=	bktr 
SRCS=	bktr_core.c bktr_os.c bktr_audio.c bktr_tuner.c bktr_card.c \
	opt_bktr.h bus_if.h device_if.h pci_if.h pcidevs.h

.include <bsd.kmod.mk>
