# $FreeBSD: src/sys/modules/aesni/Makefile,v 1.1 2010/07/23 11:00:46 kib Exp $

KMOD=	aesni
SRCS=	aesni.c aesni_wrap.c
SRCS+=	aesencdec_${MACHINE_ARCH}.S aeskeys_${MACHINE_ARCH}.S
SRCS+=	device_if.h bus_if.h cryptodev_if.h

.include <bsd.kmod.mk>
