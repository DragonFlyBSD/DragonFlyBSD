# $DragonFly: src/etc/bluetooth/Makefile,v 1.2 2008/02/12 21:36:19 hasso Exp $

FILESDIR=		/etc/bluetooth
FILES=			protocols

.if !exists(${DESTDIR}/etc/bluetooth/hosts)
FILES+=			hosts
FILESMODE_hosts=	644
.endif

.if !exists(${DESTDIR}/etc/bluetooth/bthcid.conf)
FILES+=			bthcid.conf
FILESMODE_bthcid.conf=	600
.endif

.include <bsd.prog.mk>
