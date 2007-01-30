# Used by the device build to check for device support
#
# $DragonFly: src/sys/platform/pc32/Makefile.inc,v 1.5 2007/01/30 14:50:11 corecode Exp $

DEV_SUPPORT=	acpica5 agp bridge crypto disk drm est misc netif \
		pccard powermng raid serial sound usbmisc video

SYSCONS_APM_SUPPORT=1
BOOT0CFG_SUPPORT=1

