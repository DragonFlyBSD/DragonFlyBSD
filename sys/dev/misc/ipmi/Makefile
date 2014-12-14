# $FreeBSD: head/sys/modules/ipmi/Makefile 190445 2009-03-26 17:14:22Z ambrisko $

#SUBDIR+= ipmi_linux

# XXX - ipmi_smbus and ipmi_ssif depend on smbus
# XXX - ipmi_acpi depends on acpi
KMOD=	ipmi
SRCS=	ipmi.c ipmi_kcs.c ipmi_smic.c ipmi_smbios.c ipmi_ssif.c
SRCS+=	ipmi_acpi.c ipmi_isa.c ipmi_pci.c ipmi_smbus.c
SRCS+=	opt_acpi.h
SRCS+=	acpi_if.h bus_if.h device_if.h isa_if.h pci_if.h smbus_if.h

CFLAGS+= -I${.CURDIR}/../../../contrib/dev/acpica/source/include
#CFLAGS+=-DIPMI_DEBUG -DKCS_DEBUG -DSMIC_DEBUG -DSSIF_DEBUG -DSSIF_ERROR_DEBUG -DSSIF_RETRY_DEBUG

.include <bsd.kmod.mk>
