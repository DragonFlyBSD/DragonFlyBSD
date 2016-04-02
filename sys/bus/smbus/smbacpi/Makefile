KMOD= smbacpi

SRCS= smbacpi.c
SRCS+= bus_if.h device_if.h opt_acpi.h acpi_if.h smbus_if.h

.include <bsd.kmod.mk>

.include "${SYSDIR}/dev/acpica/Makefile.inc"
