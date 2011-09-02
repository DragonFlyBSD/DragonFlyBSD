.PATH:	${.CURDIR}/../../../../dev/netif/e1000

KMOD=	if_igb
SRCS=	if_igb.c
SRCS+=	e1000_80003es2lan.c e1000_82540.c e1000_82541.c e1000_82542.c e1000_82543.c
SRCS+=	e1000_82571.c e1000_82575.c e1000_api.c e1000_ich8lan.c e1000_mac.c
SRCS+=	e1000_manage.c e1000_nvm.c e1000_osdep.c e1000_phy.c
SRCS+=	device_if.h bus_if.h pci_if.h
SRCS+=	opt_polling.h opt_ktr.h opt_inet.h

.ifndef BUILDING_WITH_KERNEL
opt_polling.h:
	echo '#define DEVICE_POLLING 1' > ${.OBJDIR}/${.TARGET}
.endif

.include <bsd.kmod.mk>
