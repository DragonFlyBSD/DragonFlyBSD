# $FreeBSD: src/sys/boot/alpha/loader/Makefile,v 1.7 2002/05/10 09:26:29 obrien Exp $
# $DragonFly: src/sys/boot/alpha/loader/Attic/Makefile,v 1.3 2003/11/10 06:08:30 dillon Exp $

BASE= 		loader
PROG= 		${BASE}
NEWVERSWHAT=	"SRM disk boot" alpha
INSTALL_HELP=	yes
LOAD_ADDRESS=	${SECONDARY_LOAD_ADDRESS}

# Only disk support
CFLAGS+=	-ffreestanding -DLOADER_DISK_SUPPORT	# -DLOADER_EXT2FS_SUPPORT

.include 	<${.CURDIR}/../common/Makefile.common>
