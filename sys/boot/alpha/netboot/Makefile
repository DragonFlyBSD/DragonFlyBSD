# $NetBSD: Makefile,v 1.12 1998/02/19 14:18:36 drochner Exp $
# $FreeBSD: src/sys/boot/alpha/netboot/Makefile,v 1.10 2002/05/10 09:26:29 obrien Exp $
# $DragonFly: src/sys/boot/alpha/netboot/Attic/Makefile,v 1.3 2003/11/10 06:08:30 dillon Exp $

BASE= 		netboot
PROG= 		${BASE}
NOMAN=
NEWVERSWHAT=	"SRM net boot" alpha
LOAD_ADDRESS=	${PRIMARY_LOAD_ADDRESS}

CFLAGS+=	-ffreestanding -DLOADER_NET_SUPPORT

.include	<${.CURDIR}/../common/Makefile.common>

