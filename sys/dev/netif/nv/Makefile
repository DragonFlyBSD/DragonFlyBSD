# $DragonFly: src/sys/dev/netif/nv/Attic/Makefile,v 1.4 2006/01/29 22:10:11 corecode Exp $

CONTRIBDIR=	${.CURDIR}/../../../contrib/dev/nv

KMOD=	if_nv
SRCS=	if_nv.c miibus_if.h device_if.h bus_if.h pci_if.h opt_polling.h
SRCS+=	adapter.h.patch basetype.h.patch os.h.patch phy.h.patch drvinfo.h.patch
OBJS+=	nvenetlib.o
CLEANFILES+= nvenetlib.o
KMODDEPS = miibus

CFLAGS+= -I${.OBJDIR} -I${.CURDIR} -I${CONTRIBDIR}

.ifndef BUILDING_WITH_KERNEL
opt_polling.h:
	echo '#define DEVICE_POLLING 1' > ${.OBJDIR}/${.TARGET}
.endif

nvenetlib.o: ${CONTRIBDIR}/${MACHINE_ARCH}/nvenetlib.o.uu
	uudecode < ${.ALLSRC:M*.uu}

.include <bsd.kmod.mk>
