# $FreeBSD: src/sys/boot/alpha/cdboot/Makefile,v 1.7 2002/05/10 09:26:28 obrien Exp $
# $DragonFly: src/sys/boot/alpha/cdboot/Attic/Makefile,v 1.3 2003/11/10 06:08:26 dillon Exp $

BASE= 		cdboot
PROG= 		${BASE}
NOMAN=
NEWVERSWHAT=	"SRM CD9660 boot" alpha
LOAD_ADDRESS=	${PRIMARY_LOAD_ADDRESS}

CFLAGS+=	-ffreestanding -DLOADER_CDROM_SUPPORT

.include <${.CURDIR}/../common/Makefile.common>
