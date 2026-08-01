KMOD=		apple_fw
KCFLAGS+=	-I${.OBJDIR}/.. -I${.CURDIR}/..
SRCS=		apple_fw.c apple_fw_dsm.c apple_fw_sysctl.c
SRCS+=		opt_acpi.h
SRCS+=		acpi_if.h device_if.h bus_if.h

.include <bsd.kmod.mk>
