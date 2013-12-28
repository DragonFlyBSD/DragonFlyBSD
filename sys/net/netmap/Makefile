# Compile netmap as a module, useful if you want a netmap bridge
# or loadable drivers.

KMOD	= netmap
SRCS	= device_if.h bus_if.h opt_netmap.h
SRCS	+= netmap.c netmap_kern.h
SRCS	+= netmap_mem2.c netmap_mem2.h
SRCS	+= netmap_generic.c
SRCS	+= netmap_mbq.c netmap_mbq.h
SRCS	+= netmap_vale.c
SRCS	+= netmap_freebsd.c

.include <bsd.kmod.mk>
