# $FreeBSD: src/sys/modules/nmdm/Makefile,v 1.1.2.1 2001/08/11 00:54:15 mp Exp $
# $DragonFly: src/sys/dev/misc/nmdm/Makefile,v 1.3 2004/08/13 17:51:05 dillon Exp $

.PATH: ${.CURDIR}/../../dev/nmdm

KMOD=	nmdm
SRCS=	nmdm.c
SRCS+=	opt_compat.h

opt_compat.h:
	echo "#define COMPAT_43 1" >opt_compat.h

.include <bsd.kmod.mk>
