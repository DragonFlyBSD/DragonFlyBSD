# $DragonFly: src/sys/bus/usb/ehci/Makefile,v 1.1 2004/03/17 18:12:56 dillon Exp $
#
# Makefile for EHCI module

.PATH: ${.CURDIR}/..
KMOD= ehci
SRCS= ehci.c ehci_pci.c opt_usb.h

CFLAGS+= -I${.OBJDIR}/..

.include <bsd.kmod.mk>
