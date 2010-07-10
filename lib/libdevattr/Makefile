LIB=	devattr
SRCS=	devattr.c devattr_device.c devattr_enumerate.c devattr_monitor.c

WARNS?=	6

LDADD+=	-lprop
DPADD+=	${LIBPROP}

NOMAN=

.include <bsd.lib.mk>
