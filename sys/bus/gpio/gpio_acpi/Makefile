KMOD= gpio_acpi

SRCS= gpio_acpi.c gpio_if.c
SRCS+= bus_if.h device_if.h opt_acpi.h acpi_if.h gpio_if.h

.include <bsd.kmod.mk>

.include "${SYSDIR}/dev/acpica/Makefile.inc"
