# $NetBSD: Makefile,v 1.2 2007/05/28 12:06:29 tls Exp $
# $DragonFly: src/usr.sbin/rfcomm_sppd/Attic/Makefile,v 1.1 2008/01/30 14:25:35 hasso Exp $

PROG=		rfcomm_sppd
SRCS=		rfcomm_sppd.c rfcomm_sdp.c

CFLAGS+=	-I${.CURDIR}/../../sys
DPADD+=		${LIBBLUETOOTH} ${LIBSDP}
LDADD+=		-lbluetooth -lsdp

.include <bsd.prog.mk>
