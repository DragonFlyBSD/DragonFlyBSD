# $FreeBSD: src/sys/modules/dc/Makefile,v 1.2 2000/01/28 11:26:28 bde Exp $

KMOD	= if_dc
SRCS	= if_dc.c device_if.h bus_if.h pci_if.h
SRCS	+= miibus_if.h
SRCS	+= opt_ifpoll.h

.ifndef KERNBUILDDIR
opt_ifpoll.h:
	echo '#define IFPOLL_ENABLE 1' > ${.OBJDIR}/${.TARGET}
.endif

.include <bsd.kmod.mk>
