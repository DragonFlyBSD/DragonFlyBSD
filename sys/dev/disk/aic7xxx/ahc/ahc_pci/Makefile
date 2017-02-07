
# $FreeBSD: src/sys/modules/aic7xxx/ahc/ahc_pci/Makefile,v 1.1.2.1 2002/09/02 05:47:45 scottl Exp $
# $DragonFly: src/sys/dev/disk/aic7xxx/ahc/ahc_pci/Makefile,v 1.3 2003/08/15 08:32:28 dillon Exp $

.PATH:	${.CURDIR}/../..
KMOD=	ahc_pci

../aic7xxx_reg.h:
	${MAKE} -C ${.CURDIR}/.. aic7xxx_reg.h

SRCS=	ahc_pci.c aic7xxx_pci.c ../aic7xxx_reg.h
SRCS+=	device_if.h bus_if.h pci_if.h
SRCS+=	opt_scsi.h opt_cam.h opt_aic7xxx.h

KCFLAGS+= -I${.CURDIR}/../.. -I..

.include <bsd.kmod.mk>
