.PATH: ${.CURDIR}/..

SYSDIR?= ${.CURDIR}/../../../..

KMOD=   sdhci_acpi
SRCS=   sdhci_acpi.c sdhci.h
SRCS+=	sdhci_if.h device_if.h bus_if.h opt_acpi.h pci_if.h acpi_if.h mmcbr_if.h

.include "../../../acpica/Makefile.inc"

.include <bsd.kmod.mk>

