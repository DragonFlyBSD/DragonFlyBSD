SUBDIR = drm amd radeon radeonfw

.if ${MACHINE_ARCH} == "x86_64"
SUBDIR+= i915 nvgpu
.endif

# empty to allow concurrent building
#
SUBDIR_ORDERED=

.include <bsd.obj.mk>
