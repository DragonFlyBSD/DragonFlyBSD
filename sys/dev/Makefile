# $DragonFly: src/sys/dev/Makefile,v 1.11 2007/01/19 07:23:42 dillon Exp $
#

.include "../platform/${MACHINE_PLATFORM}/Makefile.inc"

.ifdef DEV_ACPICA5_SUPPORT
SUBDIR+= acpica5
.endif
.ifdef DEV_AGP_SUPPORT
SUBDIR+= agp
.endif
.ifdef DEV_BRIDGE_SUPPORT
SUBDIR+= bridge
.endif
.ifdef DEV_CRYPTO_SUPPORT
SUBDIR+= crypto
.endif
.ifdef DEV_DISK_SUPPORT
SUBDIR+= disk
.endif
.ifdef DEV_DRM_SUPPORT
SUBDIR+= drm
.endif
.ifdef DEV_EST_SUPPORT
SUBDIR+= est
.endif
.ifdef DEV_MISC_SUPPORT
SUBDIR+= misc
.endif
.ifdef DEV_NETIF_SUPPORT
SUBDIR+= netif
.endif
.ifdef DEV_PCCARD_SUPPORT
SUBDIR+= pccard
.endif
.ifdef DEV_POWERMNG_SUPPORT
SUBDIR+= powermng
.endif
.ifdef DEV_RAID_SUPPORT
SUBDIR+= raid
.endif
.ifdef DEV_SERIAL_SUPPORT
SUBDIR+= serial
.endif
.ifdef DEV_SOUND_SUPPORT
SUBDIR+= sound
.endif
.ifdef DEV_USBMISC_SUPPORT
SUBDIR+= usbmisc
.endif
.ifdef DEV_VIDEO_SUPPORT
SUBDIR+= video
.endif
.ifdef DEV_VIRTUAL_SUPPORT
SUBDIR+= virtual
.endif

.include <bsd.subdir.mk>
