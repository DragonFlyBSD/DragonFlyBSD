# Makefile for the bktr Brooktree Bt848/Bt878 PCI video capture device

# $FreeBSD: src/sys/modules/bktr/bktr/Makefile,v 1.1.2.3 2001/07/24 09:51:06 dd Exp $

.PATH:  ${.CURDIR}/..
KMOD=	bktr 
SRCS=	bktr_core.c bktr_os.c bktr_audio.c bktr_tuner.c bktr_card.c \
	use_bktr.h opt_bktr.h use_smbus.h bus_if.h device_if.h \
	pci_if.h pcidevs.h

use_bktr.h:
	echo "#define NBKTR 1" > use_bktr.h
	echo "#define BKTR_FREEBSD_MODULE 1" >> use_bktr.h

# Does not use SMBUS/IICBUS. Uses the old i2c code self contained in
# the bt848 driver.
use_smbus.h:
	echo "#define NSMBUS 0" > ${.TARGET}

.include <bsd.kmod.mk>
