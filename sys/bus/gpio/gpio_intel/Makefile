KMOD= gpio_intel

SRCS=  gpio_cherryview.c gpio_intel.c
SRCS+= bus_if.h device_if.h pci_if.h opt_acpi.h acpi_if.h gpio_if.h

.include <bsd.kmod.mk>

.include "${SYSDIR}/dev/acpica/Makefile.inc"
