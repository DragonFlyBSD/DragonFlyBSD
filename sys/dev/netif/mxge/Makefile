#$FreeBSD: src/sys/modules/em/Makefile,v 1.1.2.3 2002/06/18 21:00:56 pdeuskar Exp $
#$DragonFly: src/sys/dev/netif/em/Makefile,v 1.10 2008/09/17 08:51:29 sephe Exp $

KMOD=	if_mxge
SRCS=	if_mxge.c if_mxge_var.h mcp_gen_header.h mxge_mcp.h
SRCS+=	device_if.h bus_if.h pci_if.h
SRCS+=	opt_polling.h opt_inet.h

.ifndef BUILDING_WITH_KERNEL
opt_polling.h:
	echo '#define DEVICE_POLLING 1' > ${.OBJDIR}/${.TARGET}
.endif

.include <bsd.kmod.mk>
