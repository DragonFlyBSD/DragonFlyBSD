SYSDIR?=${.CURDIR}/../../../..

.PATH: ${.CURDIR}/${MACHINE_ARCH}

KMOD=	vmbus
SRCS=	hyperv.c hyperv_busdma.c hyperv_machdep.c
SRCS+=	vmbus.c
SRCS+=	acpi_if.h bus_if.h device_if.h pcib_if.h
SRCS+=	opt_acpi.h

.include "../../../acpica/Makefile.inc"

.include <bsd.kmod.mk>
