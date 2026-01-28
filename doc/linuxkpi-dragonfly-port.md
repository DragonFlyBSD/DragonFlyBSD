# DragonFly DRM removal map (Linux-compat glue)

This document captures the removal-oriented inventory for the DRM-local Linux-compat glue in DragonFly. It is a planning guide for moving to a real FreeBSD LinuxKPI import and removing the local DRM-centric compatibility code once superseded.

## Reality check

DragonFly does not currently have a system-wide LinuxKPI. The only LinuxKPI-like code lives under the DRM subtree and exists to make DRM/KMS drivers build. Once FreeBSD LinuxKPI is imported, this local glue must be removed to avoid duplicate APIs and conflicting symbols.

## Phase 0 removal plan (detailed)

Phase 0 is the audit and readiness phase. The goal is to understand dependencies and eliminate anything that will block clean removal once a real LinuxKPI tree lands.

1) Inventory and scope
- Catalog all DRM-related code paths, including core DRM, drivers, and local Linux-compat shims. Primary buckets:
  - DRM core + drivers: `sys/dev/drm/**`
  - DRM-local Linux-compat headers: `sys/dev/drm/include/linux/**`, `sys/dev/drm/include/asm/**`, `sys/dev/drm/include/asm-generic/**`
  - DRM-local Linux-compat shims: `sys/dev/drm/linux_*.c`
  - Shared IDR used by DRM-local glue: `sys/libkern/linux_idr.c`
  - Build glue: `sys/conf/files`, `sys/conf/options`, and `sys/dev/drm/**/Makefile`

2) Kernel cross-subsystem dependency audit
- Identify places outside `sys/dev/drm` that reference DRM-local Linux glue.
- Current known dependencies that must be addressed when removing DRM glue:
  - `sys/sys/thread.h` uses `td_linux_task` (commented as drm/linux support)
  - `sys/kern/kern_exit.c` calls `linux_task_drop_callback` when `td_linux_task` is set
- Removal risk: if DRM-local task allocation is removed, ensure no remaining code sets `td_linux_task` or installs `linux_task_drop_callback`. If FreeBSD LinuxKPI is imported, decide whether it owns these fields (migrate usage) or they are removed entirely.

3) Userland and kernel interface audit
- DRM device nodes and sysctls are user-facing. Removing DRM removes:
  - `/dev/dri/cardN` device nodes
  - `drm.*` loader tunables
  - `hw.dri.*` sysctls and related debug facilities
- Userland impact: Xorg, Mesa, and libdrm from dports expect `/dev/dri` and DRM ioctls. Removing DRM will disable hardware acceleration and DRM/KMS.
- Update docs/manpages to avoid stale guidance (see `share/man/man4/drm.4`).

4) FreeBSD LinuxKPI import conflict check
- When importing FreeBSD LinuxKPI (`sys/compat/linuxkpi/`), identify APIs that overlap with DRM-local glue:
  - IDR: FreeBSD LinuxKPI ships its own `linux_idr` implementation
  - Task/thread glue: current DRM-local task allocations use `td_linux_task`
- Resolve symbol and header conflicts by removing DRM-local Linux headers/shims once LinuxKPI provides equivalents.

5) Build and configuration cleanup plan
- Enumerate build hooks to remove or refactor when DRM is removed:
  - `sys/conf/files`: all entries under `dev/drm/` including `dev/drm/ttm/*` and all `dev/drm/linux_*.c` shims
  - `sys/dev/drm/Makefile` subdirs (`drm`, `amd`, `radeon`, `radeonfw`, `i915`)
  - `sys/conf/options` DRM-related options (e.g., `DRM_DEBUG`) if unused
- Ensure no kernel configs reference `drm`, `i915`, `amdgpu`, or `radeon` after removal.

6) Documentation and user guidance update plan
- `share/man/man4/drm.4` must be removed or rewritten when DRM is gone.
- Provide a migration note for users relying on Xorg/DRI about the loss of `/dev/dri`.

7) Exit criteria for Phase 0
- Complete dependency list for DRM-local glue outside `sys/dev/drm`
- Confirm decision on `td_linux_task` and `linux_task_drop_callback` ownership
- Confirm list of userland-visible interfaces that will vanish with DRM
- Document removal order to avoid build breakage after LinuxKPI import

## Phase 0 checklist (calculated targets)

### Build glue to remove
- `sys/conf/files`: remove every line that starts with `dev/drm/` (this includes DRM core, TTM, driver sources, and all `linux_*.c` shims). This currently covers:
  - `dev/drm/*.c` (DRM core, helpers, and sysctl/sysfs glue)
  - `dev/drm/linux_*.c` (DRM-local LinuxKPI shims)
  - `dev/drm/ttm/*.c` (TTM memory manager)
  - `dev/drm/i915/*.c`, `dev/drm/radeon/*.c`, `dev/drm/amd/*.c` (drivers)
- Current line span in `sys/conf/files`: 2272-2590
- `sys/conf/options`: remove `DRM_DEBUG` if no longer referenced

### Makefiles to remove or refactor
- `sys/dev/drm/Makefile`
- `sys/dev/drm/drm/Makefile`
- `sys/dev/drm/i915/Makefile`
- `sys/dev/drm/radeon/Makefile`
- `sys/dev/drm/amd/Makefile`
- `sys/dev/drm/amd/amdgpu/Makefile`
- `sys/dev/drm/radeonfw/**/Makefile`

### Kernel cross-subsystem cleanups
- `sys/sys/thread.h`: remove or migrate `td_linux_task` once LinuxKPI owns task lifecycle
- `sys/kern/kern_exit.c`: remove or migrate `linux_task_drop_callback` dispatch

### Userland/documentation updates
- `share/man/man4/drm.4`: remove or replace with a deprecation note
- Migration note: Xorg/Mesa/libdrm will lose `/dev/dri/cardN`, `drm.*` loader tunables, and `hw.dri.*` sysctls. Document fallback expectations (no hardware acceleration / no KMS).

## Removal map (what to delete when ripping out DRM + glue)

If the intent is to remove everything related to the DRM stack (i915/amdgpu/radeon and the associated Linux-compat glue), the core buckets to delete are:

- DRM core + drivers: `sys/dev/drm/**`
- DRM-local Linux-compat headers: `sys/dev/drm/include/linux/**`, `sys/dev/drm/include/asm/**`, `sys/dev/drm/include/asm-generic/**`
- DRM-local Linux-compat shims: `sys/dev/drm/linux_*.c`
- Shared IDR used by those headers/shims: `sys/libkern/linux_idr.c`

This list is meant to pair with `doc/linuxkpi-freebsd-impl.md`, which explains what the glue implements.
