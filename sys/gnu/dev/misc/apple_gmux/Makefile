KMOD=	apple_gmux
SRCS=	apple-gmux.c
SRCS+=	device_if.h opt_acpi.h acpi_if.h bus_if.h pci_if.h
SRCS+=	opt_ktr.h opt_drm.h

KCFLAGS+= -I${SYSDIR}/dev/drm/include

.include <bsd.kmod.mk>
