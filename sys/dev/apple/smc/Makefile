KMOD=		apple_smc
KCFLAGS+=	-I${.OBJDIR}/.. -I${.CURDIR}/..
SRCS=		smc.c smc_io.c smc_mmio.c smc_sysctl.c smc_thermal.c opt_apple_smc.h opt_acpi.h acpi_if.h device_if.h bus_if.h

.include <bsd.kmod.mk>
