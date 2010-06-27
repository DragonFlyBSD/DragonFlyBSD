# $FreeBSD: src/sys/modules/vn/Makefile,v 1.5.2.1 2001/07/24 09:49:43 dd Exp $ 
# $DragonFly: src/sys/dev/disk/vn/Makefile,v 1.3 2004/08/13 17:51:05 dillon Exp $ 
DEBUG_FLAGS=-g
DEBUG=-g
KMOD=   dm
SRCS=   device-mapper.c dm_dev.c dm_ioctl.c dm_pdev.c dm_table.c \
	dm_target.c
SRCS+=	bus_if.h device_if.h

SRCS+=	dm_target_error.c dm_target_linear.c dm_target_zero.c dm_target_stripe.c dm_target_crypt.c

.include <bsd.kmod.mk>
