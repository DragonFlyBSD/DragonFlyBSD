# $FreeBSD: src/sys/modules/mfi/Makefile,v 1.6 2010/08/23 06:13:29 imp Exp $

SUBDIR= mfip

#.if ${MACHINE_ARCH} == "i386" || ${MACHINE_ARCH} == "x86_64"
.if ${MACHINE_ARCH} == "i386"
SUBDIR+= mfi_linux
.endif

KMOD=	mfi
SRCS=	mfi.c mfi_pci.c mfi_disk.c mfi_debug.c mfi_syspd.c mfi_tbolt.c
SRCS+=	opt_mfi.h opt_cam.h
SRCS+=	device_if.h bus_if.h pci_if.h

#CFLAGS += -DMFI_DEBUG

.include <bsd.kmod.mk>
