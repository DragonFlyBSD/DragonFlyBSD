# $FreeBSD: src/sys/modules/ccd/Makefile,v 1.9 1999/11/28 18:52:42 bde Exp $ 
# $DragonFly: src/sys/dev/disk/ccd/Makefile,v 1.3 2003/08/15 08:32:29 dillon Exp $ 

.PATH:  ${.CURDIR}/../../dev/ccd
KMOD=   ccd
SRCS=   ccd.c use_ccd.h vnode_if.h
NOMAN=

NCCD?=          4
CLEANFILES=	use_ccd.h

use_ccd.h:
	echo "#define NCCD ${NCCD}" > use_ccd.h

.include <bsd.kmod.mk>
