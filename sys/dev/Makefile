.include "../platform/${MACHINE_PLATFORM}/Makefile.inc"

SUBDIR=	\
	acpica5 \
	agp \
	crypto \
	disk \
	drm \
	misc \
	netif \
	pccard \
	powermng \
	raid \
	serial \
	sound \
	usbmisc \
	video \
	virtual

.for dir in ${SUBDIR}
.if empty(DEV_SUPPORT:M${dir}) && empty(DEV_SUPPORT:M${dir}/*)
SUBDIR:= ${SUBDIR:N${dir}}
.endif
.endfor

.include <bsd.subdir.mk>
