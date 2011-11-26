# $FreeBSD: src/lib/libdevinfo/Makefile,v 1.4 2002/05/13 12:11:54 ru Exp $
# $DragonFly: src/lib/libdevinfo/Makefile,v 1.2 2008/09/30 13:13:11 swildner Exp $

LIB=	devinfo
SRCS=	devinfo.c
INCS=	devinfo.h
MAN=	devinfo.3
WARNS?=	3

MLINKS+=devinfo.3 devinfo_foreach_device_child.3 \
	devinfo.3 devinfo_foreach_device_resource.3 \
	devinfo.3 devinfo_foreach_rman.3 \
	devinfo.3 devinfo_foreach_rman_resource.3 \
	devinfo.3 devinfo_free.3 \
	devinfo.3 devinfo_handle_to_device.3 \
	devinfo.3 devinfo_handle_to_resource.3 \
	devinfo.3 devinfo_handle_to_rman.3 \
	devinfo.3 devinfo_init.3

.include <bsd.lib.mk>
