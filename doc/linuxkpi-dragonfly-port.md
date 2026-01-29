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

## Phase 0 execution todo

- [x] Remove `dev/drm/*` build glue from `sys/conf/files` (line span 2272-2590).
- [x] Remove DRM options from `sys/conf/options` (drop `DRM_DEBUG`/`VGA_SWITCHEROO`).
- [x] Remove DRM-local thread/proc hooks (`td_linux_task`, `p_linux_mm`, and drop callbacks).
- [x] Remove `share/man/man4/drm.4` to avoid stale userland guidance.
- [x] Remove `drm.4` entry from `share/man/man4/Makefile`.
- [x] Remove `sys/libkern/linux_idr.c` to avoid conflicts with FreeBSD LinuxKPI.
- [x] Remove DRM Makefiles to prevent build recursion into DRM subtrees.
- [x] Remove `drm` from `sys/dev/Makefile` subdir list.
- [x] Remove DRM devices/options from `sys/config/LINT64`.
- [x] Remove `sys/dev/drm/**` sources from the repository.

Note: `sys/dev/drm/**` sources are removed; this leaves only non-DRM consumers to be addressed by the incoming FreeBSD LinuxKPI import.

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

## Phase 0.A: Verification Build

After removing the DRM stack and Linux-compat glue in Phase 0, we must verify that DragonFly still builds correctly and no stale references remain.

### Goals

1. Ensure `buildworld` completes successfully
2. Ensure `buildkernel` completes successfully  
3. Verify no stale references to removed DRM symbols
4. Confirm kernel boots without DRM-related errors
5. Validate that no kernel configs reference removed devices

### Development Environment Setup

#### VM Access

The DragonFly VM is accessible via SSH:
```bash
ssh -p 6021 root@devbox
```

Source code location on VM: `/usr/src`

#### Syncing Changes to VM

The workflow for syncing changes from local repository to VM:

1. **Push changes to gitea remote** (from local repo):
   ```bash
   git push gitea port-linuxkpi
   ```

2. **Update VM source** (on VM):
   ```bash
   cd /usr/src
   git fetch gitea port-linuxkpi
   git reset --hard gitea/port-linuxkpi
   ```

3. **Clean and rebuild** (if needed):
   ```bash
   rm -rf /usr/obj/*
   make -j$(sysctl -n hw.ncpu) buildworld
   make -j$(sysctl -n hw.ncpu) buildkernel KERNCONF=X86_64_GENERIC
   ```

### Build Verification Steps

#### Step 1: Clean Build Environment
```bash
# Clean previous build artifacts
cd /usr/src
rm -rf /usr/obj/*
make cleanworld  # if needed
```

#### Step 2: Build World
```bash
cd /usr/src
make -j$(sysctl -n hw.ncpu) buildworld
```

**Success Criteria:**
- Build completes without errors
- No warnings about missing DRM headers or symbols
- All userland utilities compile successfully

#### Step 3: Build Kernel

```bash
cd /usr/src
make -j$(sysctl -n hw.ncpu) buildkernel KERNCONF=X86_64_GENERIC
```

**Success Criteria:**
- Kernel compiles without errors
- No undefined references to `drm_*`, `i915_*`, `amdgpu_*`, `radeon_*`
- No warnings about missing `linux_*` symbols
- All kernel modules build successfully

#### Step 4: Check for Stale References

After build, verify no stale references exist:

```bash
# Check for DRM references in kernel configs
grep -r "device.*drm\|device.*i915\|device.*amdgpu\|device.*radeon" /usr/src/sys/config/

# Check for DRM options in kernel configs  
grep -r "options.*DRM\|options.*VGA_SWITCHEROO" /usr/src/sys/config/

# Verify no DRM in linker sets
nm /usr/obj/usr/src/sys/X86_64_GENERIC/kernel.debug | grep -E "drm_|i915_|amdgpu_|radeon_"

# Check for linux_task_drop_callback references
nm /usr/obj/usr/src/sys/X86_64_GENERIC/kernel.debug | grep linux_task_drop
```

**Expected Results:**
- All grep commands should return empty (no matches)
- Kernel should have no unresolved DRM symbols

#### Step 5: Install and Test Boot (Optional but Recommended)

```bash
# Install the new kernel
make installkernel KERNCONF=X86_64_GENERIC

# Reboot to test
reboot
```

**Verification After Boot:**
```bash
# Check kernel boot messages
dmesg | grep -i drm
dmesg | grep -i i915
dmesg | grep -i amdgpu

# Verify no /dev/dri devices exist
ls -la /dev/dri  # Should return "No such file or directory"

# Check for any DRM-related errors in logs
cat /var/log/messages | grep -i drm
```

### Common Issues and Fixes Found During Phase 0.A

| Issue | Cause | Fix |
|-------|-------|-----|
| `cd: /usr/src/sys/dev/drm/include/linux: No such file or directory` | include/Makefile tries to install DRM headers | Remove DRM header installation rules from include/Makefile |
| `fatal error: linux/slab.h: No such file or directory` (agp) | agp module includes DRM headers | Remove agp from kernel config (X86_64_GENERIC) |
| `fatal error: linux/fb.h: No such file or directory` (vga) | vga_switcheroo requires DRM headers | Remove video from DEV_SUPPORT in sys/platform/pc64/Makefile.inc |
| `fatal error: linux/types.h: No such file or directory` (apple_gmux) | apple_gmux requires DRM headers | Remove apple_gmux from build (clear sys/gnu/dev/misc/Makefile) |
| `cc1: error: /usr/src/sys/dev/drm/include: No such file or directory` | Kernel includes DRM paths | Remove INCLUDES from sys/conf/kern.pre.mk |
| `fatal error: drm/drm.h: No such file or directory` (kdump) | kdump includes DRM headers for ioctl decoding | Remove DRM includes from usr.bin/kdump/Makefile and mkioctls |
| `fatal error: drm/drm.h: No such file or directory` (truss) | truss includes DRM headers | Remove DRM include from usr.bin/truss/Makefile |
| `fatal error: uapi_drm/drm.h: No such file or directory` (testvblank) | testvblank test requires DRM | Remove testvblank from test/debug/Makefile |

### Success Criteria for Phase 0.A

- [ ] `buildworld` completes without errors
- [ ] `buildkernel` completes without errors
- [ ] No stale DRM references in kernel configs
- [ ] Kernel boots successfully
- [ ] No DRM-related errors in dmesg
- [ ] `/dev/dri` does not exist (as expected after removal)

### Exit Criteria

Once Phase 0.A is complete, DragonFly is ready for Phase 1 (LinuxKPI import). The system should:
1. Build cleanly without any DRM remnants
2. Boot and run normally without DRM functionality
3. Have no stale references to removed symbols
4. Be in a clean state to receive the FreeBSD LinuxKPI import

---

## Phase 1: FreeBSD LinuxKPI Import Analysis

Phase 1 involves analyzing the FreeBSD LinuxKPI implementation to identify required subsystems and determining whether DragonFly should port them directly or provide compatibility layers.

### FreeBSD LinuxKPI Structure Overview

FreeBSD LinuxKPI is located at `sys/compat/linuxkpi/` and consists of:
- **common/include/**: 234+ Linux API headers (`linux/`, `asm/`, `net/`, `crypto/`, etc.)
- **common/src/**: 47+ implementation files (~30,000 lines)
- **dummy/include/**: 75+ stub headers for unimplemented APIs
- **modules/**: 4 kernel modules (linuxkpi, linuxkpi_wlan, linuxkpi_hdmi, linuxkpi_video)

### Required Subsystems Analysis

| Subsystem | FreeBSD Implementation | DragonFly Status | Recommendation |
|-----------|------------------------|------------------|----------------|
| **RCU (Read-Copy-Update)** | `linux_rcu.c` using `ck_epoch` (Concurrency Kit) | **MISSING**: No ck_epoch library | Port `ck_epoch` to DragonFly OR implement RCU using DragonFly's native synchronization |
| **Backlight Control** | `linux_pci.c` uses `backlight_if.h` interface | **MISSING**: No backlight subsystem | Port FreeBSD's backlight subsystem OR stub for initial bringup |
| **I2C Bus Interface** | `linux_i2c.c` uses `iicbus_if.h` | **PARTIAL**: Has I2C but interface may differ | Adapt DragonFly I2C interface to match FreeBSD's `iicbus_if` |
| **USB Stack** | `linux_usb.c` maps to FreeBSD USB | **DIFFERENT**: DragonFly uses "u4b" vs FreeBSD's USB | Provide USB compatibility layer or port FreeBSD USB stack |
| **Workqueues** | `linux_work.c` maps to FreeBSD `taskqueue` | **COMPATIBLE**: Has `taskqueue.h` from FreeBSD heritage | Port directly - minor adaptations needed |
| **SLAB Allocator** | `linux_slab.c` maps to `malloc` with `M_LINUXKM` | **COMPATIBLE**: Has compatible malloc | Port directly |
| **PCI Subsystem** | `linux_pci.c` (~52K lines) | **PARTIAL**: Has PCI but lacks some interfaces | Port and adapt - requires `pcib_if.h`, MSI/MSI-X support |
| **Device Model** | `linux_kobject.c`, device resources | **MISSING**: No kobject/sysfs equivalent | Port kobject layer OR stub for initial bringup |
| **DMA-BUF** | `linux_dma-buf-map.h` | **MISSING**: No DMA buffer sharing | Port if graphics drivers needed |
| **802.11 Wireless** | `linux_80211.c` (~254K lines) | **MAJOR EFFORT**: Would require full wireless stack | Defer - not required for initial bringup |

### Critical Infrastructure Gaps

#### 1. Concurrency Kit (ck_epoch) - RCU Support

**Problem:** FreeBSD LinuxKPI uses `ck_epoch` from the Concurrency Kit library for RCU implementation. DragonFly does not have this library.

**Options:**
- **Option A (Recommended):** Port Concurrency Kit (`sys/contrib/ck/`) to DragonFly
  - Pros: Full compatibility with FreeBSD LinuxKPI, battle-tested RCU
  - Cons: ~6,000 lines of additional code, new dependency
- **Option B:** Implement RCU using DragonFly's existing synchronization primitives
  - Pros: No new dependencies
  - Cons: Must maintain separately, potential subtle differences in semantics

**Decision:** Port Concurrency Kit to DragonFly's `sys/contrib/ck/` for maximum compatibility.

#### 2. Backlight Subsystem

**Problem:** FreeBSD has a `backlight` driver subsystem (`sys/dev/backlight/`) that LinuxKPI uses for display backlight control. DragonFly lacks this.

**Options:**
- **Option A:** Port FreeBSD backlight subsystem
  - Pros: Full compatibility
  - Cons: Additional subsystem to maintain
- **Option B:** Provide stub/stub interface initially
  - Pros: Faster bringup
  - Cons: Missing functionality for display drivers

**Decision:** Provide stub backlight interface for Phase 1, full port in Phase 2 if graphics drivers are needed.

#### 3. USB Stack Compatibility

**Problem:** FreeBSD LinuxKPI's `linux_usb.c` maps to FreeBSD's USB stack (`dev/usb/`). DragonFly's USB stack (u4b) may have different APIs.

**Options:**
- **Option A:** Port FreeBSD's USB stack to DragonFly
  - Pros: Maximum compatibility
  - Cons: Massive effort, duplicates existing USB support
- **Option B:** Adapt LinuxKPI USB layer to DragonFly's u4b
  - Pros: Uses existing infrastructure
  - Cons: Requires significant modifications to `linux_usb.c`

**Decision:** TBD - needs analysis of DragonFly's u4b vs FreeBSD USB API differences.

#### 4. Device Model (kobject/sysfs)

**Problem:** LinuxKPI implements Linux's device model with kobjects, ksets, and sysfs-like functionality. DragonFly has no equivalent.

**Options:**
- **Option A:** Port FreeBSD's kobject implementation
  - Pros: Full compatibility
  - Cons: Complex subsystem (~354 lines in `linux_kobject.c`)
- **Option B:** Minimal stub implementation
  - Pros: Faster bringup
  - Cons: May break drivers that rely on sysfs

**Decision:** Port kobject layer - it's required for most LinuxKPI functionality.

### Build System Requirements

FreeBSD LinuxKPI requires these build system features:

1. **Module Makefiles** - Need to support:
   ```makefile
   CFLAGS+= ${LINUXKPI_INCLUDES}
   SRCS+=   ${LINUXKPI_GENSRCS}
   ```

2. **Generated Interface Files** - Need to generate:
   - `bus_if.h`, `device_if.h`, `pci_if.h`
   - `iicbus_if.h`, `iicbb_if.h`
   - `backlight_if.h` (if backlight ported)
   - `lkpi_iic_if.c`, `lkpi_iic_if.h`

3. **Include Paths** - Need to add:
   - `-I${SYSDIR}/compat/linuxkpi/common/include`
   - `-I${SYSDIR}/compat/linuxkpi/dummy/include`
   - `-include ${SYSDIR}/compat/linuxkpi/common/include/linux/kconfig.h`

### Implementation Priority

#### Phase 1A: Core (Minimum Viable)
Required for basic driver support:
1. Port Concurrency Kit (ck_epoch) for RCU
2. Port core compatibility (`linux_compat.c`)
3. Port memory allocation (`linux_slab.c`)
4. Port synchronization (`linux_lock.c`, `linux_rcu.c`)
5. Port workqueues (`linux_work.c`)
6. Port data structures (IDR, XArray, radix trees)

#### Phase 1B: Bus Support
Required for PCI/USB/I2C drivers:
1. Port PCI layer (`linux_pci.c`)
2. Port I2C layer (`linux_i2c.c`, `linux_i2cbb.c`)
3. Port USB layer (adapt to DragonFly's u4b OR port FreeBSD USB)

#### Phase 1C: Network (Optional)
Only needed for networking drivers:
1. Port socket buffer layer (`linux_skbuff.c`)
2. Port netdevice layer (`linux_netdev.c`)
3. Port 802.11 layer (`linux_80211.c`) - LARGE

#### Phase 1D: Graphics (Optional)
Only needed for graphics/display drivers:
1. Port backlight subsystem
2. Port HDMI/CEC support
3. Port aperture support
4. Port DMA-BUF

### Recommendations Summary

**Subsystem Strategy:**

| Subsystem | Strategy | Effort |
|-----------|----------|--------|
| Concurrency Kit | Port directly to `sys/contrib/ck/` | Medium |
| RCU | Use ported ck_epoch | Low (after ck) |
| Workqueues | Port directly (uses taskqueue) | Low |
| Memory | Port directly (uses malloc) | Low |
| PCI | Port with adaptations | Medium |
| I2C | Port with interface adaptations | Medium |
| USB | Needs analysis - likely adaptation | High |
| Backlight | Stub initially, port if needed | Low (stub) |
| Kobject/Device | Port directly | Medium |
| Networking | Defer to Phase 2 | High |
| Graphics/DRM | Defer to Phase 2 | High |

**Next Steps:**
1. Port Concurrency Kit to DragonFly
2. Import FreeBSD LinuxKPI headers to `sys/compat/linuxkpi/`
3. Port core implementation files (compat, slab, lock, rcu, work)
4. Create stub implementations for missing subsystems (backlight)
5. Port PCI and I2C subsystems
6. Build and test minimal kernel module

---

## Phase 1 (Addendum): DRM-KMOD Specific Requirements

**Goal:** Enable drm-kmod (external repo) to build and run on DragonFly. This is the PRIMARY objective - graphics drivers take priority over wireless/networking drivers.

### DRM-KMOD Overview

The drm-kmod repository at `~/s/drm-kmod/` contains:
- **Core DRM**: Modern Linux DRM stack (GEM, atomic, sync objects)
- **Intel i915**: Intel graphics driver (x86/x86_64 only)
- **AMD GPU**: amdgpu driver (amd64, aarch64, powerpc64, riscv64)
- **Radeon**: Legacy AMD driver
- **TTM**: GPU memory manager
- **DMA-BUF**: Buffer sharing infrastructure

### Linux Headers Required by DRM-KMOD (199 total)

#### Tier 1: Critical for DRM (Must Have)
These headers are absolutely required for any DRM functionality:

**Core Foundation:**
- `linux/compiler_types.h`, `linux/types.h`, `linux/kernel.h`
- `linux/module.h`, `linux/moduleparam.h`, `linux/init.h`
- `linux/export.h`, `linux/errno.h`, `linux/err.h`
- `linux/stddef.h`, `linux/string.h`, `linux/ctype.h`

**Data Structures:**
- `linux/list.h`, `linux/llist.h`, `linux/rbtree.h`
- `linux/idr.h`, `linux/xarray.h`, `linux/radix-tree.h`
- `linux/kref.h`, `linux/refcount.h`

**Memory Management:**
- `linux/mm.h`, `linux/slab.h`, `linux/gfp.h`, `linux/vmalloc.h`
- `linux/dma-mapping.h`, `linux/dma-buf.h`, `linux/scatterlist.h`
- `linux/shmem_fs.h`, `linux/mempool.h`

**Synchronization:**
- `linux/spinlock.h`, `linux/mutex.h`, `linux/rwsem.h`, `linux/ww_mutex.h`
- `linux/wait.h`, `linux/completion.h`, `linux/barrier.h`
- `linux/atomic.h`, `linux/rcupdate.h`, `linux/srcu.h`

**Device Model:**
- `linux/device.h`, `linux/kobject.h`, `linux/sysfs.h`
- `linux/pci.h`, `linux/platform_device.h`, `linux/acpi.h`
- `linux/dmi.h`, `linux/firmware.h`

**Graphics-Specific:**
- `linux/dma-fence.h`, `linux/dma-resv.h`, `linux/sync_file.h`
- `linux/fb.h`, `linux/backlight.h`, `linux/aperture.h`
- `linux/hdmi.h`, `linux/cec.h`, `linux/vgaarb.h`

#### Tier 2: Important for Full Functionality
- `linux/i2c.h`, `linux/gpio/consumer.h`, `linux/pinctrl/consumer.h`
- `linux/pm.h`, `linux/pm_runtime.h`
- `linux/debugfs.h`, `linux/seq_file.h`
- `linux/agp_backend.h`, `linux/iommu.h`

#### Tier 3: Optional/Stubbable
- `linux/hwmon.h`, `linux/thermal.h`, `linux/power_supply.h`
- `linux/input.h`, `linux/tty.h`, `linux/console.h`
- `linux/tracepoint.h` (debugging)

### FreeBSD LinuxKPI Files Required for DRM

From `sys/compat/linuxkpi/common/src/`, these files are **essential**:

#### Core (Non-Negotiable)
| File | Purpose | Lines | Priority |
|------|---------|-------|----------|
| `linux_compat.c` | Basic compatibility | ~3,000 | CRITICAL |
| `linux_slab.c` | Memory allocation | ~350 | CRITICAL |
| `linux_idr.c` | ID allocation | ~830 | CRITICAL |
| `linux_xarray.c` | XArray (modern IDR) | ~450 | CRITICAL |
| `linux_radix.c` | Radix tree (legacy) | ~600 | HIGH |
| `linux_lock.c` | Locking primitives | ~200 | CRITICAL |
| `linux_rcu.c` | RCU (needs ck_epoch) | ~460 | CRITICAL |
| `linux_work.c` | Workqueues | ~800 | CRITICAL |
| `linux_wait.c` | Wait queues | ~400 | CRITICAL |
| `linux_kthread.c` | Kernel threads | ~180 | HIGH |
| `linux_kobject.c` | Device model | ~350 | CRITICAL |

#### Memory & DMA (Critical for GPU)
| File | Purpose | Lines | Priority |
|------|---------|-------|----------|
| `linux_page.c` | Page operations | ~770 | HIGH |
| `linux_shmemfs.c` | Shared memory | ~120 | HIGH |
| `linux_dma_buf.c` | DMA buffer sharing | ~800 | CRITICAL |
| `linux_fence.c` | DMA fences | ~600 | CRITICAL |

#### PCI & Bus Support
| File | Purpose | Lines | Priority |
|------|---------|-------|----------|
| `linux_pci.c` | PCI subsystem | ~2,200 | CRITICAL |
| `linux_i2c.c` | I2C bus | ~380 | HIGH |
| `linux_i2cbb.c` | I2C bit-banging | ~330 | MEDIUM |

#### Graphics-Specific
| File | Purpose | Lines | Priority |
|------|---------|-------|----------|
| `linux_aperture.c` | Graphics aperture | ~390 | HIGH |
| `linux_backlight.c` | Backlight control | ~200 | HIGH |
| `linux_hdmi.c` | HDMI/CEC | ~1,960 | MEDIUM |

#### Timer & IRQ
| File | Purpose | Lines | Priority |
|------|---------|-------|----------|
| `linux_hrtimer.c` | High-res timers | ~140 | HIGH |
| `linux_interrupt.c` | IRQ handling | ~250 | HIGH |
| `linux_tasklet.c` | Tasklets | ~280 | HIGH |

#### Power Management
| File | Purpose | Lines | Priority |
|------|---------|-------|----------|
| `linux_pm.c` | Power management | ~500 | HIGH |
| `linux_firmware.c` | Firmware loading | ~250 | HIGH |
| `linux_dmi.c` | DMI/SMBIOS | ~150 | MEDIUM |
| `linux_acpi.c` | ACPI integration | ~400 | MEDIUM |

#### Debug/Support
| File | Purpose | Lines | Priority |
|------|---------|-------|----------|
| `linux_debugfs.c` | Debug filesystem | ~400 | MEDIUM |
| `linux_seq_file.c` | Seq file interface | ~200 | MEDIUM |
| `linux_kmod.c` | Module support | ~35 | CRITICAL |

### DragonFly-Specific Challenges for DRM

#### 1. Concurrency Kit (ck_epoch) - CRITICAL
**Status:** DragonFly does not have `ck_epoch` from Concurrency Kit.

**Why it matters for DRM:**
- GPU drivers heavily use RCU for object tracking
- DMA reservations use SRCU (sleepable RCU)
- i915 and amdgpu both rely on RCU for hot paths

**Decision:** **MUST PORT** - DRM cannot function without proper RCU.

#### 2. DMA-BUF / DMA-Fence - CRITICAL
**Status:** DragonFly has no DMA buffer sharing infrastructure.

**Why it matters:**
- Core DRM functionality for buffer sharing between drivers
- Required for PRIME (GPU buffer export/import)
- Required for dma-buf sync file support

**Decision:** **MUST PORT** - Core DRM functionality.

#### 3. Backlight Subsystem - HIGH
**Status:** DragonFly has no backlight control subsystem.

**Why it matters:**
- Required for laptop display brightness control
- i915 and amdgpu both need this
- Users expect working brightness keys

**Decision:** Port FreeBSD's `sys/dev/backlight/` subsystem.

#### 4. Aperture / Graphics Memory - HIGH
**Status:** DragonFly removed DRM-local aperture code.

**Why it matters:**
- Required for legacy graphics memory access
- AGP support (older GPUs)
- Some Intel and AMD chips need this

**Decision:** Port `linux_aperture.c` from FreeBSD LinuxKPI.

#### 5. PCI P2P (Peer-to-Peer) - MEDIUM
**Status:** Unclear if DragonFly supports PCI peer-to-peer DMA.

**Why it matters:**
- AMD GPU uses P2P for multi-GPU setups
- SmartNICs and accelerators use this

**Decision:** Investigate DragonFly's PCI P2P support; may need implementation.

#### 6. IOMMU Integration - MEDIUM
**Status:** DragonFly has IOMMU but interface may differ.

**Why it matters:**
- AMD GPU uses IOMMU for security
- Required for device isolation

**Decision:** Port LinuxKPI IOMMU wrappers with DragonFly adaptations.

#### 7. Floating Point in Kernel - MEDIUM
**Status:** DragonFly may not support FP in kernel context.

**Why it matters:**
- AMD Display Core (DC) requires FP support
- DC is 1000+ files of display code

**Decision:** Investigate DragonFly FP-in-kernel support; may need workarounds.

### Updated Implementation Priority for DRM Focus

#### Phase 1A: Core Foundation - COMPLETED ✓
**Goal:** Port Concurrency Kit (CK) for RCU support

**Status:** COMPLETED - CK has been successfully ported to DragonFly

**Completed Tasks:**
1. ✓ Created vendor/CK branch with upstream CK 0.7.0 (commit 2265c784)
2. ✓ Applied FreeBSD exclusions (doc/, regressions/, tools/, etc.)
3. ✓ Merged vendor/CK to port-linuxkpi branch
4. ✓ Added DragonFly-specific modifications:
   - `sys/contrib/ck/include/ck_md.h` - Machine-dependent configuration
   - Modified standard headers for DragonFly kernel support
   - Fixed `ck_hp.c` to use DragonFly's `kqsort()`/`kbsearch()`
5. ✓ Added build integration to `sys/conf/files` and `sys/conf/kern.pre.mk`
6. ✓ Added README.DRAGONFLY and README.DELETED

**Verification:**
- ✓ Kernel builds successfully with CK
- ✓ CK symbols present in kernel (`_ck_epoch_addref`, `_ck_epoch_delref`)
- ✓ No build errors

---

#### Phase 1B: Import LinuxKPI Headers and Core Implementation
**Goal:** Import FreeBSD LinuxKPI headers and core implementation files

**Approach:** Big bang import from FreeBSD commit 79b05e7f80eb482287c700f10da9084824199a05
**Architecture:** x86_64 only for now
**Branch:** port-linuxkpi

**Tasks:**

#### Phase 1B: Memory & Synchronization (Weeks 3-4)
**Goal:** GPU memory management support

1. Port memory management:
   - `linux_page.c` (page operations)
   - `linux_shmemfs.c` (shared memory)
2. Port DMA infrastructure:
   - `linux_dma_buf.c` (DMA buffer sharing)
   - `linux_fence.c` (DMA fences)
   - `linux_sync_file.c` (sync file support)
3. Port threading:
   - `linux_kthread.c`
   - `linux_tasklet.c`
   - `linux_interrupt.c`
4. Port timers:
   - `linux_hrtimer.c`

**Success Criteria:** Can allocate and share DMA buffers.

#### Phase 1C: Bus & Device Support (Weeks 5-6)
**Goal:** PCI/GPU device discovery and initialization

1. Port PCI subsystem:
   - `linux_pci.c` (full PCI support)
   - Generate `pci_if.h`, `pcib_if.h`
2. Port I2C support:
   - `linux_i2c.c`, `linux_i2cbb.c`
   - Generate `iicbus_if.h`
3. Port platform/ACPI:
   - `linux_platform.c` (if exists)
   - `linux_acpi.c`
   - `linux_dmi.c`
4. Port firmware loading:
   - `linux_firmware.c`

**Success Criteria:** Can probe and attach PCI GPUs.

#### Phase 1D: Graphics-Specific (Weeks 7-8)
**Goal:** Display and output support

1. Port backlight subsystem:
   - Port FreeBSD `sys/dev/backlight/`
   - Port `linux_backlight.c`
   - Generate `backlight_if.h`
2. Port aperture support:
   - `linux_aperture.c`
3. Port video/HDMI:
   - `linux_hdmi.c`
4. Port power management:
   - `linux_pm.c`

**Success Criteria:** Backlight control and basic display output work.

#### Phase 1E: Build Integration (Week 9)
**Goal:** DRM-KMOD can build against DragonFly LinuxKPI

1. Update `sys/conf/kmod.mk`:
   - Add `LINUXKPI_INCLUDES`
   - Add `LINUXKPI_GENSRCS`
2. Create module Makefiles:
   - `sys/modules/linuxkpi/Makefile`
   - `sys/modules/linuxkpi_video/Makefile` (for graphics)
3. Generate interface files:
   - `bus_if.h`, `device_if.h`
   - `pci_if.h`, `pcib_if.h`
   - `iicbus_if.h`, `backlight_if.h`
4. Test build of drm-kmod:
   - Start with `dummygfx` (simplest driver)
   - Progress to `radeon` (simpler than amdgpu/i915)
   - Eventually `i915` and `amdgpu`

**Success Criteria:** drm-kmod repository can build against DragonFly.

### DRM-KMOD Files Not Needed (Defer)

These FreeBSD LinuxKPI files are **NOT** needed for DRM focus:

| File | Purpose | Why Not Needed |
|------|---------|----------------|
| `linux_80211*.c` | Wireless 802.11 | Only for WiFi drivers |
| `linux_netdev.c` | Network devices | Only for Ethernet drivers |
| `linux_skbuff.c` | Socket buffers | Only for networking |
| `linux_usb.c` | USB stack | USB graphics rare; defer |
| `linux_mhi.c` | Modem Host Interface | Not for graphics |
| `linux_eventfd.c` | Eventfd | Can stub initially |

### Success Metrics for Phase 1

1. **Build Success:** `linuxkpi.ko` builds without errors
2. **Load Success:** Module loads and basic symbols resolve
3. **DRM-KMOD Build:** External drm-kmod repo can build
4. **Basic GPU Detection:** Can probe Intel/AMD GPUs (no display yet)
5. **Display Output:** Framebuffer console or X11 works on at least one GPU

### Risk Assessment

| Risk | Impact | Mitigation |
|------|--------|------------|
| ck_epoch port complexity | HIGH | Use FreeBSD's ck contrib directly |
| DMA-BUF implementation | HIGH | Study FreeBSD implementation carefully |
| FP in kernel for AMD DC | MEDIUM | Test early; may need DC workarounds |
| DragonFly PCI differences | MEDIUM | Adapt `linux_pci.c` as needed |
| Thread/scheduler differences | MEDIUM | Test RCU integration thoroughly |

