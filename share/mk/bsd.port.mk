# $FreeBSD: src/share/mk/bsd.port.mk,v 1.303.2.2 2002/07/17 19:08:23 ru Exp $
# $DragonFly: src/share/mk/Attic/bsd.port.mk,v 1.2 2003/06/17 04:37:02 dillon Exp $

PORTSDIR?=	/usr/ports

.include <bsd.own.mk>
.include "${PORTSDIR}/Mk/bsd.port.mk"
