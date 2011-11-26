# $FreeBSD: src/sys/dev/mii/miibus_if.m,v 1.2 1999/08/28 00:42:14 peter Exp $
# $DragonFly: src/sys/dev/netif/mii_layer/miibus_if.m,v 1.3 2003/11/17 00:54:40 asmodai Exp $

#include <sys/bus.h>

INTERFACE miibus;

#
# Read register from device on MII bus
#
METHOD int readreg {
	device_t		dev;
	int			phy;
	int			reg;
};

#
# Write register to device on MII bus
#
METHOD int writereg {
	device_t		dev;
	int			phy;
	int			reg;
	int			val;
};

#
# Notify bus about PHY status change.
#
METHOD void statchg {
	device_t		dev;
};

#
# Notify bus that media has been set.
#
METHOD void mediainit {
	device_t		dev;
};
