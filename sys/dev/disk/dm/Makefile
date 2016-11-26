KMOD=   dm
SRCS=   device-mapper.c dm_dev.c dm_ioctl.c dm_pdev.c dm_table.c \
	dm_target.c

SRCS+=	dm_target_error.c dm_target_zero.c

SUBDIR= linear striped crypt delay flakey

.include <bsd.kmod.mk>
