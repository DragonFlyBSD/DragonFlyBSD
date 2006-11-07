# $DragonFly: src/sys/dev/Makefile,v 1.8 2006/11/07 19:56:21 dillon Exp $
#

.include "../machine/${MACHINE}/Makefile.inc"

SUBDIR=agp bridge crypto disk drm misc netif pccard powermng raid \
	serial sound usbmisc video

.ifdef ACPICA5_SUPPORT
SUBDIR+= acpica5
.endif
.ifdef EST_SUPPORT
SUBDIR+= est
.endif

.include <bsd.subdir.mk>
