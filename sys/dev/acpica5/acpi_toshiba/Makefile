# $FreeBSD: src/sys/modules/acpi/acpi_toshiba/Makefile,v 1.1 2004/01/11 19:18:38 njl Exp $
# $DragonFly: src/sys/dev/acpica5/acpi_toshiba/Makefile,v 1.1 2004/06/27 08:52:43 dillon Exp $

.include "../Makefile.inc"
.PATH:		${.CURDIR}/../../../${MACHINE_ARCH}/acpica5

KMOD=		acpi_toshiba
CFLAGS+=	-I${.OBJDIR} -I${.OBJDIR}/.. -I${.CURDIR}/.. \
		-I${.CURDIR}/../../../${SYSACPICA}/include
SRCS=		acpi_toshiba.c opt_acpi.h device_if.h bus_if.h

.include <bsd.kmod.mk>
