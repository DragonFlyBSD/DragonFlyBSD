# $FreeBSD: src/sys/modules/fxp/Makefile,v 1.9.2.3 2001/12/04 20:01:53 brooks Exp $

KMOD	= if_fxp
SRCS	= if_fxp.c device_if.h bus_if.h pci_if.h miibus_if.h
SRCS	+= opt_ifpoll.h

.ifndef KERNBUILDDIR
opt_ifpoll.h:
	echo '#define IFPOLL_ENABLE 1' > ${.OBJDIR}/${.TARGET}
.endif

.include <bsd.kmod.mk>
