SUBDIR= drm

.if ${CCVER} == "gcc44" || ${CCVER} == "gcc47"
SUBDIR+= radeon radeonfw
.endif

.if ${MACHINE_ARCH} == "x86_64"
SUBDIR+= i915
.endif

.include <bsd.obj.mk>
