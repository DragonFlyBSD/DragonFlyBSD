# $FreeBSD: src/sys/modules/ip_mroute_mod/Makefile,v 1.5 1999/08/28 00:47:20 peter Exp $
# $DragonFly: src/sys/net/ip_mroute/Makefile,v 1.4 2005/08/15 16:46:20 dillon Exp $

.PATH:	${.CURDIR}/../../netinet
KMOD=	ip_mroute
SRCS=	ip_mroute.c opt_mrouting.h
KCFLAGS+= -DMROUTE_LKM -DMROUTING

opt_mrouting.h:
	echo "#define MROUTING 1" > ${.TARGET}

.include <bsd.kmod.mk>
