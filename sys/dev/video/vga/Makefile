KMOD=	vga_switcheroo
SRCS=	vga_switcheroo.c
SRCS+=	opt_ddb.h device_if.h bus_if.h pci_if.h

KCFLAGS+= -I${SYSDIR}/dev/drm/include
KCFLAGS+= -I${SYSDIR}/dev/drm/include/uapi

.include <bsd.kmod.mk>
