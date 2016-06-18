# NVME Driver Makefile
#
KMOD= nvme

SRCS= nvme.c nvme_dragonfly.c nvme_attach.c nvme_admin.c nvme_disk.c \
      nvme_ioctl.c \
      bus_if.h device_if.h pci_if.h pcidevs.h

CFLAGS+= -DINVARIANTS

.include <bsd.kmod.mk>
