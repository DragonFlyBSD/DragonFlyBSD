# $FreeBSD: src/sys/modules/ip_mroute_mod/Makefile,v 1.5 1999/08/28 00:47:20 peter Exp $
# $DragonFly: src/sys/net/ip_mroute/Makefile,v 1.3 2003/08/15 07:03:10 dillon Exp $

.PATH:	${.CURDIR}/../../netinet
KMOD=	ip_mroute
SRCS=	ip_mroute.c opt_mrouting.h opt_random_ip_id.h
NOMAN=
CFLAGS+= -DMROUTE_LKM -DMROUTING

opt_mrouting.h:
	echo "#define MROUTING 1" > ${.TARGET}

opt_random_ip_id.h:
	echo > ${.TARGET}

.include <bsd.kmod.mk>
