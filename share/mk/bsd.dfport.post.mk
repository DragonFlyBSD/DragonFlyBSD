# $FreeBSD: src/share/mk/bsd.port.post.mk,v 1.3.2.1 2002/07/17 19:08:23 ru Exp $
# $DragonFly: src/share/mk/Attic/bsd.dfport.post.mk,v 1.1 2003/10/11 21:08:33 dillon Exp $

AFTERPORTMK=	yes

.include <bsd.dfport.mk>

.undef AFTERPORTMK
