# $FreeBSD: src/sys/modules/dummynet/Makefile,v 1.1.2.2 2003/04/08 10:18:00 maxim Exp $
# $DragonFly: src/sys/net/dummynet/Makefile,v 1.7 2008/09/16 12:30:57 sephe Exp $

KMOD=	dummynet3
SRCS=	ip_dummynet3.c
SRCS+=	opt_ipdn.h

.ifndef BUILDING_WITH_KERNEL
opt_ipdn.h:
	echo '#define DUMMYNET 1' > ${.OBJDIR}/${.TARGET}
	# Debug prints
	#echo '#define DUMMYNET_DEBUG 1' >> ${.OBJDIR}/${.TARGET}
.endif

.include <bsd.kmod.mk>
