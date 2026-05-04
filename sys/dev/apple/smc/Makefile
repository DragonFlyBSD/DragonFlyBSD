KMOD=		apple_smc
KCFLAGS+=	-I${.OBJDIR}/.. -I${.CURDIR}/..
SRCS=		smc.c smc_io.c smc_mmio.c smc_sysctl.c opt_apple_smc.h acpi_if.h device_if.h bus_if.h

.include <bsd.kmod.mk>
