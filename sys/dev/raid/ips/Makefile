# $DragonFly: src/sys/dev/raid/ips/Makefile,v 1.1 2004/01/15 15:41:23 drhodus Exp $

.PATH:	${.CURDIR}/../../dev/raid/ips
KMOD	= ips
SRCS	= ips.c ips.h ips_commands.c ips_disk.c ips_disk.h ips_ioctl.c \
	ips_ioctl.h ips_pci.c device_if.h bus_if.h use_ips.h pci_if.h
CLEANFILES	= use_ips.h

use_ips.h:
	echo "#define NIPS 1" > use_ips.h

.include <bsd.kmod.mk>
