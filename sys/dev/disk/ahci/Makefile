# Native AHCI driver, ported from OpenBSD
#
KMOD= ahci

SRCS= ahci_dragonfly.c ahci_attach.c ahci_cam.c ahci_pm.c ahci.c \
      bus_if.h device_if.h pci_if.h opt_scsi.h opt_cam.h pcidevs.h

.include <bsd.kmod.mk>
