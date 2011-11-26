# $FreeBSD: src/sys/modules/acpi/acpi_toshiba/Makefile,v 1.1 2004/01/11 19:18:38 njl Exp $
# $DragonFly: src/sys/dev/acpica5/acpi_toshiba/Makefile,v 1.5 2008/09/06 21:21:31 hasso Exp $

KMOD=		acpi_toshiba
CFLAGS+=	-I${.OBJDIR}/.. -I${.CURDIR}/..
SRCS=		acpi_toshiba.c opt_acpi.h device_if.h bus_if.h

.include <bsd.kmod.mk>
