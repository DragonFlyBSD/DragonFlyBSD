#$FreeBSD: src/sys/modules/ixgbe/Makefile,v 1.12 2012/05/30 13:55:43 uqs Exp $

.include <bsd.own.mk>

KMOD    = if_ixgbe
SRCS    = device_if.h bus_if.h pci_if.h
SRCS    += opt_inet.h opt_inet6.h
SRCS    += ixgbe.c #ixv.c
# Shared source
SRCS    += ixgbe_common.c ixgbe_api.c ixgbe_phy.c ixgbe_mbx.c ixgbe_vf.c
SRCS    += ixgbe_82599.c ixgbe_82598.c ixgbe_x540.c
CFLAGS+= -DSMP #-DIXGBE_FDIR

.if !defined(BUILDING_WITH_KERNEL)
opt_inet.h:
	@echo "#define INET 1" > ${.TARGET}

opt_inet6.h:
	@echo "#define INET6 1" > ${.TARGET}
.endif

.include <bsd.kmod.mk>
