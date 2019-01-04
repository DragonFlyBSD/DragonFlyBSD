# $FreeBSD: src/sys/modules/i2c/smbus/Makefile,v 1.1 2002/03/23 15:48:59 nsouch Exp $

.PATH:		${.CURDIR}/..
KMOD		= smbus
SRCS		= device_if.h bus_if.h smbus_if.h smbus_if.c \
		  smbconf.c smbus.c

.include <bsd.kmod.mk>
