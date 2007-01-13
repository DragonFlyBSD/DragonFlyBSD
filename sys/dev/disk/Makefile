# $DragonFly: src/sys/dev/disk/Makefile,v 1.2 2007/01/13 23:57:04 dillon Exp $
#

.ifndef DEV_DISK_LIMITED_SUPPORT

SUBDIR= aha aic7xxx ccd ispfw md ncv nsp sbp stg trm vn vpo

.else

SUBDIR=
.ifdef DEV_DISK_AHA_SUPPORT
SUBDIR+= aha
.endif
.ifdef DEV_DISK_AIC7XXX_SUPPORT
SUBDIR+= aic7xxx
.endif
.ifdef DEV_DISK_CCD_SUPPORT
SUBDIR+= ccd
.endif
.ifdef DEV_DISK_ISPFW_SUPPORT
SUBDIR+= ispfw
.endif
.ifdef DEV_DISK_MD_SUPPORT
SUBDIR+= md
.endif
.ifdef DEV_DISK_NCV_SUPPORT
SUBDIR+= ncv
.endif
.ifdef DEV_DISK_NSP_SUPPORT
SUBDIR+= nsp
.endif
.ifdef DEV_DISK_SBP_SUPPORT
SUBDIR+= sbp
.endif
.ifdef DEV_DISK_STG_SUPPORT
SUBDIR+= stg
.endif
.ifdef DEV_DISK_TRM_SUPPORT
SUBDIR+= trm
.endif
.ifdef DEV_DISK_VN_SUPPORT
SUBDIR+= vn
.endif
.ifdef DEV_DISK_VPO_SUPPORT
SUBDIR+= vpo
.endif

.endif

.include <bsd.subdir.mk>
