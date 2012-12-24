KMOD=   virtio

SRCS=   virtio.c virtqueue.c virtio_if.h virtio_bus_if.h device_if.h bus_if.h\
	virtio_bus_if.c virtio_if.c

MFILES= kern/bus_if.m kern/device_if.m
MFILES+=dev/virtual/virtio/virtio/virtio_if.m
MFILES+=dev/virtual/virtio/virtio/virtio_bus_if.m

.include <bsd.kmod.mk>
