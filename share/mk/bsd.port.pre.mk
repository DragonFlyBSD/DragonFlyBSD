# $FreeBSD: src/share/mk/bsd.port.pre.mk,v 1.3.2.1 2002/07/17 19:08:23 ru Exp $
# $DragonFly: src/share/mk/Attic/bsd.port.pre.mk,v 1.2 2003/06/17 04:37:02 dillon Exp $

BEFOREPORTMK=	yes

.include <bsd.port.mk>

.undef BEFOREPORTMK
