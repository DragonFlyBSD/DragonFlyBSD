# $DragonFly: src/sys/dev/disk/Makefile,v 1.4 2008/01/05 08:16:04 pavalos Exp $
#

.include "${.CURDIR}/../../platform/${MACHINE_PLATFORM}/Makefile.inc"

SUBDIR= aha aic aic7xxx ccd ispfw md ncv nsp sbp stg trm vn vpo

.for dir in ${SUBDIR}
.if empty(DEV_SUPPORT:Mdisk) && \
	empty(DEV_SUPPORT:Mdisk/${dir}) && empty(DEV_SUPPORT:Mdisk/${dir}/*)
SUBDIR:= ${SUBDIR:N${dir}}
.endif
.endfor

.include <bsd.subdir.mk>
