# $DragonFly: src/etc/dma/Makefile,v 1.2.2.1 2008/02/12 22:12:08 matthias Exp $

FILESDIR=	/etc/dma
SHAREOWN=	root
SHAREGRP=	mail
FILESMODE=	640

.if !exists(${DESTDIR}/etc/dma/auth.conf)
FILES+=		auth.conf
.endif
.if !exists(${DESTDIR}/etc/dma/dma.conf)
FILES+=		dma.conf
.endif
.if !exists(${DESTDIR}/etc/dma/virtusertable)
FILES+=		virtusertable
.endif

.include <bsd.prog.mk>
