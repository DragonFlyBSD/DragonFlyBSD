#
# XXX temporarily include subr_input.c until it is
# stable enough to just compile into the kernel.

.PATH: ${.CURDIR}/../../../kern
KMOD=	cyapa
SRCS=	cyapa.c
SRCS+=	device_if.h bus_if.h smbus_if.h

.include <bsd.kmod.mk>
