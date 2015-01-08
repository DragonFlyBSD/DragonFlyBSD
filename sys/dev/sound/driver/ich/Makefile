# $FreeBSD: head/sys/modules/sound/driver/ich/Makefile 245952 2013-01-26 22:08:21Z pfg $

.PATH:		${.CURDIR}/../../../../dev/sound/pci
KMOD		= snd_ich
SRCS		= device_if.h bus_if.h pci_if.h
SRCS		+= ich.c

.include <bsd.kmod.mk>
