# $DragonFly: src/share/mk/Attic/bsd.dfport.mk,v 1.2 2003/09/09 16:49:31 dillon Exp $

PORTSDIR?=	/usr/dfports

.include <bsd.own.mk>
.include "${PORTSDIR}/Mk/bsd.port.mk"

