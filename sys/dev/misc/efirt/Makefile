# $FreeBSD: head/sys/modules/efirt/Makefile 307070 2016-10-11 22:24:30Z imp $

.PATH: ${.CURDIR}/../../../platform/${MACHINE_PLATFORM}/${MACHINE_ARCH}

KMOD=	efirt
SRCS=	efirt.c efidev.c
SRCS+=  device_if.h bus_if.h

.include <bsd.kmod.mk>
