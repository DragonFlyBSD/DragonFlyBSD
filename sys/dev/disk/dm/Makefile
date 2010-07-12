KMOD=   dm
SRCS=   device-mapper.c dm_dev.c dm_ioctl.c dm_pdev.c dm_table.c \
	dm_target.c
SRCS+=	bus_if.h device_if.h

SRCS+=	dm_target_error.c dm_target_linear.c dm_target_zero.c dm_target_stripe.c dm_target_crypt.c

.include <bsd.kmod.mk>
