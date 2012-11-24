# $FreeBSD: src/sys/modules/if_ndis/Makefile,v 1.6 2005/09/27 18:10:35 mlaier Exp $

KMOD=	if_ndis
SRCS=	if_ndis.c if_ndis_pci.c if_ndis_pccard.c
SRCS+=	device_if.h bus_if.h pci_if.h card_if.h
SRCS+=	opt_usb.h use_usb4bsd.h

.if defined(WANT_USB4BSD)
SRCS+=	if_ndis_u4b.c
.else
SRCS+=	if_ndis_usb.c
.endif

.if !defined(BUILDING_WITH_KERNEL) && defined(WANT_USB4BSD)
use_usb4bsd.h:
	echo "#define NUSB4BSD 1" > ${.TARGET}
.endif

.include <bsd.kmod.mk>
