# Native AHCI driver, ported from OpenBSD
#
KMOD= sili

SRCS= sili_dragonfly.c sili_attach.c sili_cam.c sili_pm.c sili.c \
      bus_if.h device_if.h pci_if.h opt_scsi.h opt_cam.h pcidevs.h

.include <bsd.kmod.mk>
