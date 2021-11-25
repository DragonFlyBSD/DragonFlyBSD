# $FreeBSD: src/sys/modules/dummynet/Makefile,v 1.1.2.2 2003/04/08 10:18:00 maxim Exp $

KMOD=   dummynet
SRCS=   ip_dummynet.c
SRCS+=	opt_ipdn.h

.ifndef KERNBUILDDIR
opt_ipdn.h:
	echo '#define DUMMYNET 1' > ${.OBJDIR}/${.TARGET}
	# Debug prints
	#echo '#define DUMMYNET_DEBUG 1' >> ${.OBJDIR}/${.TARGET}
.endif

.include <bsd.kmod.mk>
