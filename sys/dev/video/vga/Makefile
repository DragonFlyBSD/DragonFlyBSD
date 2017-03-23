KMOD=	vga_switcheroo
SRCS=	vga_switcheroo.c
SRCS+=	device_if.h bus_if.h pci_if.h opt_drm.h

KCFLAGS+= -I${SYSDIR}/dev/drm/include

.include <bsd.kmod.mk>
