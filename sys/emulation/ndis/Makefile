# $FreeBSD: src/sys/modules/ndis/Makefile,v 1.14 2010/08/23 06:13:29 imp Exp $

KMOD=	ndis
SRCS=	subr_pe.c subr_ndis.c subr_hal.c subr_ntoskrnl.c kern_ndis.c
SRCS+=	kern_windrv.c subr_u4bd.c
SRCS+=	device_if.h bus_if.h pci_if.h
SRCS+=	opt_usb.h

.if ${MACHINE_ARCH} == "x86_64"
SRCS+=	winx64_wrap.S
.endif

.if ${MACHINE_ARCH} == "i386"
SRCS+=	winx32_wrap.S
.endif

.include <bsd.kmod.mk>
