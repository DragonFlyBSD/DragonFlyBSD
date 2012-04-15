# $FreeBSD: src/sys/modules/nmdm/Makefile,v 1.1.2.1 2001/08/11 00:54:15 mp Exp $

KMOD=	nmdm
SRCS=	nmdm.c
SRCS+=	opt_compat.h

opt_compat.h:
	echo "#define COMPAT_43 1" >${.TARGET}

.include <bsd.kmod.mk>
