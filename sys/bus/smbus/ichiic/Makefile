# This driver is for the intel generation 4 IIC controller,
# not to be mixed up with the SMB controller.  We implement
# the IIC controller with the SMBUS bus interface because
# the IIC bus interface is too primitive for this controller.
#

SYSDIR?= ${.CURDIR}/../../..

KMOD= ig4

SRCS= ig4_acpi.c ig4_pci.c ig4_iic.c
SRCS+= bus_if.h device_if.h opt_acpi.h acpi_if.h pci_if.h smbus_if.h pcidevs.h

.include "../../../dev/acpica/Makefile.inc"

.include <bsd.kmod.mk>
