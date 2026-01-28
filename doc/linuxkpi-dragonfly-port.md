# DragonFly DRM removal map (Linux-compat glue)

This document captures the removal-oriented inventory for the DRM-local Linux-compat glue in DragonFly. It is intentionally brief and actionable.

## Removal map (what to delete when ripping out DRM + glue)

If the intent is to remove everything related to the DRM stack (i915/amdgpu/radeon and the associated Linux-compat glue), the core buckets to audit are:

- DRM core + drivers: `sys/dev/drm/**`
- DRM-local Linux-compat headers: `sys/dev/drm/include/linux/**`, `sys/dev/drm/include/asm/**`
- DRM-local Linux-compat shims: `sys/dev/drm/linux_*.c`
- Shared IDR used by those headers/shims: `sys/libkern/linux_idr.c`

This list is meant to pair with `doc/linuxkpi-freebsd-impl.md`, which explains what the glue implements.
