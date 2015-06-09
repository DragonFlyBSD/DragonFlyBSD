.include "${.CURDIR}/../../platform/${MACHINE_PLATFORM}/Makefile.inc"

SUBDIR= ahci aic aic7xxx ccd dm iscsi isp ispfw md mmcsd mpt nata ncv nsp
SUBDIR+= sbp sdhci sili stg sym trm vn vpo

.for dir in ${SUBDIR}
.if empty(DEV_SUPPORT:Mdisk) && \
	empty(DEV_SUPPORT:Mdisk/${dir}) && empty(DEV_SUPPORT:Mdisk/${dir}/*)
SUBDIR:= ${SUBDIR:N${dir}}
.endif
.endfor

# Empty for concurrent build
#
SUBDIR_ORDERED=

.include <bsd.subdir.mk>
