SUBDIR = drm radeon radeonfw

.if ${MACHINE_ARCH} == "x86_64"
SUBDIR+= i915
.endif

# empty to allow concurrent building
#
SUBDIR_ORDERED=

.include <bsd.obj.mk>
