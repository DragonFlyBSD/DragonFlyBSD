# $FreeBSD: src/sys/modules/coda/Makefile,v 1.7.2.1 2001/07/24 09:49:42 dd Exp $
# $DragonFly: src/sys/vfs/coda/Attic/Makefile,v 1.4 2004/08/13 17:51:10 dillon Exp $

KMOD=	coda
SRCS=	coda_fbsd.c coda_namecache.c coda_psdev.c coda_subr.c \
	coda_venus.c coda_vfsops.c coda_vnops.c use_vcoda.h
NOMAN=

CLEANFILES=	use_vcoda.h

use_vcoda.h:
	echo "#define NVCODA 4" > use_vcoda.h

.include <bsd.kmod.mk>
