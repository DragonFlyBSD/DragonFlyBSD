# Compile netmap as a module, useful if you want a netmap bridge
# or loadable drivers.

KMOD	= netmap
SRCS	= device_if.h bus_if.h opt_netmap.h
SRCS	+= netmap.c
SRCS	+= netmap_mem2.c
SRCS	+= netmap_generic.c
SRCS	+= netmap_mbq.c
SRCS	+= netmap_vale.c
SRCS	+= netmap_freebsd.c

.include <bsd.kmod.mk>
