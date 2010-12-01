DM_PATH=	${.CURDIR}/../../

KMOD=   dm_target_crypt

SRCS+=	bus_if.h device_if.h
SRCS+=	dm_target_crypt.c
CFLAGS+=-I${DM_PATH}

.include <bsd.kmod.mk>
