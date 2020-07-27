KMOD=	apple_gmux
SRCS=	apple-gmux.c
SRCS+=	opt_acpi.h opt_ddb.h opt_drm.h opt_ktr.h
SRCS+=	acpi_if.h bus_if.h device_if.h pci_if.h

KCFLAGS+= -I${SYSDIR}/dev/drm/include
KCFLAGS+= -I${SYSDIR}/dev/drm/include/uapi
KCFLAGS+= -include ${SYSDIR}/dev/drm/kconfig.h

.include <bsd.kmod.mk>
