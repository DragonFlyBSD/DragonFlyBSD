
# $FreeBSD: src/sys/modules/aic7xxx/ahc/ahc_eisa/Makefile,v 1.1.2.1 2002/09/02 05:47:45 scottl Exp $
# $DragonFly: src/sys/dev/disk/aic7xxx/ahc/ahc_eisa/Makefile,v 1.4 2007/07/06 04:56:22 pavalos Exp $

.PATH:	${.CURDIR}/../..
KMOD=		ahc_eisa

../aic7xxx_reg.h:
	( cd .. ; ${MAKE} aic7xxx_reg.h )

SRCS=	ahc_eisa.c ../aic7xxx_reg.h
SRCS+=	device_if.h bus_if.h
SRCS+=	opt_scsi.h opt_cam.h opt_aic7xxx.h

CFLAGS+=	-I${.CURDIR}/../../ -I..

CLEANFILES=	../aic7xxx_reg.h ../aic7xxx_seq.h

.include <bsd.kmod.mk>
