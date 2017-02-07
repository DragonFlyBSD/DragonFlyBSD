# $FreeBSD: src/sys/modules/acpi/acpi_hp/Makefile,v 1.1 2009/06/23 13:17:25 rpaulo Exp $

KMOD=	acpi_hp
KCFLAGS+=-I${.CURDIR}/..
SRCS=	acpi_hp.c opt_acpi.h acpi_if.h acpi_wmi_if.h device_if.h bus_if.h

.include <bsd.kmod.mk>
