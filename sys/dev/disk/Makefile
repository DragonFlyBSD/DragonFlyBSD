.include "${.CURDIR}/../../platform/${MACHINE_PLATFORM}/Makefile.inc"

SUBDIR= ahci ccd dm iscsi isp md mmcsd mpt nata
SUBDIR+= nvme sbp sdhci sili sym trm vn vpo

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
