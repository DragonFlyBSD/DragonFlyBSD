# $FreeBSD: src/sys/modules/pcic/Makefile,v 1.13 1999/12/19 05:49:56 imp Exp $
# $DragonFly: src/sys/dev/misc/pcic/Attic/Makefile,v 1.3 2003/08/15 08:32:29 dillon Exp $

.PATH:  ${.CURDIR}/../../../bus/pccard
KMOD=   pcic
SRCS=   i82365.c i82365_isa.c \
	device_if.h bus_if.h isa_if.h power_if.h card_if.h
NOMAN=

.include <bsd.kmod.mk>
