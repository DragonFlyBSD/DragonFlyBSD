SYSDIR?=${.CURDIR}/../../../..

KMOD=	vmbus
SRCS=	hyperv.c vmbus.c
SRCS+=	acpi_if.h bus_if.h device_if.h
SRCS+=	opt_acpi.h

.include "../../../acpica/Makefile.inc"

.include <bsd.kmod.mk>
