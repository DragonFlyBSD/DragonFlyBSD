# dst
# $FreeBSD: src/sbin/dhclient/dst/Makefile,v 1.2.2.2 2002/07/19 18:46:27 ru Exp $
# $DragonFly: src/sbin/dhclient/dst/Attic/Makefile,v 1.3 2005/01/04 19:58:54 joerg Exp $

.include "${.CURDIR}/../Makefile.common"

LIB=	 dst
INTERNALLIB=		true

SRCS=	base64.c dst_api.c dst_support.c hmac_link.c md5_dgst.c prandom.c

CFLAGS+= -DHMAC_MD5 -DMINIRES_LIB

.include <bsd.lib.mk>
