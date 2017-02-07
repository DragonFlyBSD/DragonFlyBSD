
# $FreeBSD: src/sys/modules/aic7xxx/ahd/Makefile,v 1.2.2.1 2002/09/02 05:47:45 scottl Exp $
# $DragonFly: src/sys/dev/disk/aic7xxx/ahd/Makefile,v 1.4 2005/04/21 14:43:58 joerg Exp $

.PATH:	${.CURDIR}/..
KMOD=	ahd

BEFORE_DEPEND= aic79xx_{seq,reg}.h

../aicasm/aicasm.nx:
	( cd ${.CURDIR}/../aicasm; ${MAKE} aicasm.nx; )

aic79xx_{seq.h,reg.h,reg_print.c}:					\
		${.CURDIR}/../aic79xx.{reg,seq}	\
		${.CURDIR}/../../../../bus/cam/scsi/scsi_message.h ../aicasm/aicasm.nx
	${.OBJDIR}/../aicasm/aicasm.nx ${INCLUDES} -I${.CURDIR}/../../../../bus/cam/scsi \
			-I${.CURDIR}/..		 \
			-o aic79xx_seq.h -r aic79xx_reg.h		 \
			-p aic79xx_reg_print.c				 \
			-i ${.CURDIR}/../aic79xx_osm.h \
			${.CURDIR}/../aic79xx.seq

GENSRCS= aic79xx_reg.h aic79xx_seq.h

SRCS= ${GENSRCS}
AHD_REG_PRETTY_PRINT=1
.ifdef AHD_REG_PRETTY_PRINT
SRCS+= aic79xx_reg_print.c
KCFLAGS+= -DAHD_REG_PRETTY_PRINT=1
.endif
SRCS+= aic79xx.c aic79xx_osm.c aic79xx_pci.c ahd_pci.c
SRCS+= opt_scsi.h opt_aic79xx.h opt_cam.h opt_ddb.h
SRCS+= device_if.h bus_if.h pci_if.h

CLEANFILES= ${GENSRCS} aic79xx_reg_print.c

.include <bsd.kmod.mk>
