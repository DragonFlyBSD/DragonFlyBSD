
# $FreeBSD: src/sys/modules/aic7xxx/ahc/Makefile,v 1.2.2.1 2002/09/02 05:47:44 scottl Exp $
# $DragonFly: src/sys/dev/disk/aic7xxx/ahc/Makefile,v 1.3 2003/08/15 08:32:28 dillon Exp $

.PATH:	${.CURDIR}/..
KMOD=	ahc
SUBDIR=	ahc_eisa ahc_pci

BEFORE_DEPEND= aic7xxx_{seq,reg}.h

../aicasm/aicasm:
	( cd ${.CURDIR}/../aicasm; ${MAKE} aicasm; )

aic7xxx_{seq.h,reg.h,reg_print.c}:					\
		${.CURDIR}/../aic7xxx.{reg,seq}	\
		${.CURDIR}/../../../../bus/cam/scsi/scsi_message.h ../aicasm/aicasm
	${.OBJDIR}/../aicasm/aicasm ${INCLUDES} -I${.CURDIR}/../../../../bus/cam/scsi \
			-I${.CURDIR}/..					 \
			-o aic7xxx_seq.h -r aic7xxx_reg.h		 \
			-p aic7xxx_reg_print.c				 \
			-i ${.CURDIR}/../aic7xxx_osm.h \
			${.CURDIR}/../aic7xxx.seq

GENSRCS= aic7xxx_reg.h aic7xxx_seq.h use_pci.h

AHC_REG_PRETTY_PRINT=1
SRCS= ${GENSRCS}
.ifdef AHC_REG_PRETTY_PRINT
SRCS+= aic7xxx_reg_print.c
CFLAGS+= -DAHC_REG_PRETTY_PRINT=1
.endif
SRCS+= aic7xxx.c aic7xxx_93cx6.c aic7xxx_osm.c
SRCS+= opt_scsi.h opt_aic7xxx.h opt_cam.h
SRCS+= device_if.h bus_if.h pci_if.h

CLEANFILES= ${GENSRCS} aic7xxx_reg_print.c

use_pci.h:
	echo "#define NPCI 1" > use_pci.h

.include <bsd.kmod.mk>
