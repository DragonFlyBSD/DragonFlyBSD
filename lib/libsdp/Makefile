# $NetBSD: Makefile,v 1.2 2007/05/28 12:06:21 tls Exp $
# $FreeBSD: src/lib/libsdp/Makefile,v 1.4 2005/07/22 17:19:03 kensmith Exp $
# $DragonFly: src/lib/libsdp/Makefile,v 1.2 2008/03/01 23:58:00 swildner Exp $

SYSDIR= ${.CURDIR}/../../sys/

LIB=		sdp
SHLIB_MAJOR=	2
MAN=		sdp.3
WARNS?=		2

SRCS=		search.c service.c session.c util.c
INCS=		sdp.h
INCSDIR=	/usr/include

CFLAGS+=	-I${.CURDIR} -I${SYSDIR}

MLINKS+=	sdp.3 SDP_GET8.3
MLINKS+=	sdp.3 SDP_GET16.3
MLINKS+=	sdp.3 SDP_GET32.3
MLINKS+=	sdp.3 SDP_GET64.3
MLINKS+=	sdp.3 SDP_GET128.3
MLINKS+=	sdp.3 SDP_GET_UUID128.3
MLINKS+=	sdp.3 SDP_PUT8.3
MLINKS+=	sdp.3 SDP_PUT16.3
MLINKS+=	sdp.3 SDP_PUT32.3
MLINKS+=	sdp.3 SDP_PUT64.3
MLINKS+=	sdp.3 SDP_PUT128.3
MLINKS+=	sdp.3 SDP_PUT_UUID128.3
MLINKS+=	sdp.3 sdp_open.3
MLINKS+=	sdp.3 sdp_open_local.3
MLINKS+=	sdp.3 sdp_close.3
MLINKS+=	sdp.3 sdp_error.3
MLINKS+=	sdp.3 sdp_search.3
MLINKS+=	sdp.3 sdp_attr2desc.3
MLINKS+=	sdp.3 sdp_uuid2desc.3
MLINKS+=	sdp.3 sdp_register_service.3
MLINKS+=	sdp.3 sdp_unregister_service.3
MLINKS+=	sdp.3 sdp_change_service.3

.include <bsd.lib.mk>
