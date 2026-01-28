# FreeBSD LinuxKPI Implementation Guide

This document describes FreeBSD's LinuxKPI (Linux Kernel Programming Interface) implementation, a system-wide compatibility layer that enables Linux kernel drivers to compile and run on FreeBSD without modification.

## Overview

FreeBSD LinuxKPI is a comprehensive compatibility subsystem located at `sys/compat/linuxkpi/`. Unlike DragonFly's DRM-local approach, FreeBSD LinuxKPI is system-wide and supports multiple driver types including networking, wireless, crypto, USB, and graphics.

## Directory Structure

```
sys/compat/linuxkpi/
├── common/
│   ├── include/
│   │   ├── linux/          # 232+ Linux API headers
│   │   ├── asm/            # Architecture-specific (x86, arm)
│   │   ├── asm-generic/    # Generic ASM implementations
│   │   ├── net/            # Networking (cfg80211, mac80211)
│   │   ├── acpi/           # ACPI headers
│   │   ├── video/          # Video/display headers
│   │   └── crypto/         # Crypto API headers
│   └── src/                # 47 implementation files
│       ├── linux_compat.c  # Core compatibility layer
│       ├── linux_pci.c     # PCI subsystem
│       ├── linux_usb.c     # USB subsystem
│       ├── linux_work.c    # Work queues
│       ├── linux_80211.c   # Wireless 802.11
│       ├── linux_skbuff.c  # Socket buffers
│       └── ...
└── dummy/
    └── include/            # Placeholder headers for unimplemented APIs
```

## Kernel Modules

LinuxKPI builds four kernel modules:

| Module | Location | Description | Dependencies |
|--------|----------|-------------|--------------|
| `linuxkpi` | `sys/modules/linuxkpi/` | Core implementation | firmware, pci, usb, iicbus, iicbb, backlight |
| `linuxkpi_wlan` | `sys/modules/linuxkpi_wlan/` | 802.11 wireless support | linuxkpi, wlan |
| `linuxkpi_hdmi` | `sys/modules/linuxkpi_hdmi/` | HDMI support | linuxkpi |
| `linuxkpi_video` | `sys/modules/linuxkpi_video/` | Video/aperture support | linuxkpi |

## Build System Integration

### Module Makefiles

Modules using LinuxKPI must include:

```makefile
CFLAGS+= ${LINUXKPI_INCLUDES}
SRCS+=   ${LINUXKPI_GENSRCS}
```

### Build Variables (from `sys/conf/kmod.mk`)

- `LINUXKPI_INCLUDES`: Adds include paths and auto-includes `linux/kconfig.h`
  - `-I${SYSDIR}/compat/linuxkpi/common/include`
  - `-I${SYSDIR}/compat/linuxkpi/dummy/include`
  - `-include ${SYSDIR}/compat/linuxkpi/common/include/linux/kconfig.h`

- `LINUXKPI_GENSRCS`: Auto-generated interface files
  - `bus_if.h`, `device_if.h`, `pci_if.h`, `usb_if.h`
  - `iicbus_if.h`, `iicbb_if.h`, `lkpi_iic_if.c`, `lkpi_iic_if.h`
  - `backlight_if.h`, `vnode_if.h`, etc.

## Consumer Drivers

The following drivers depend on LinuxKPI modules:

### Networking
- **Mellanox**: mlx4, mlx5 (core, en, ib, fpga)
- **Chelsio**: iw_cxgbe

### Cryptography
- **Intel QAT**: qat_common, qat_hw variants

### Wireless
- Various wireless drivers via `linuxkpi_wlan` module using cfg80211/mac80211

### USB
- Various USB drivers using `linux_usb.c` integration

## Core Components

### Task/Current and Scheduling

**Headers:**
- `include/linux/sched.h` - Task states, scheduling primitives
- `include/linux/current.h` - Current task pointer
- `include/linux/kthread.h` - Kernel thread API

**Implementation:**
- `src/linux_current.c` - Current task implementation
- `src/linux_kthread.c` - kthread_create, kthread_stop
- `src/linux_schedule.c` - schedule(), schedule_timeout()

**Key Patterns:**
- Uses FreeBSD `struct thread` wrapped in Linux `struct task_struct`
- Maps Linux task states to FreeBSD thread states

### Memory Management

**Headers:**
- `include/linux/slab.h` - kmalloc/kfree family
- `include/linux/mm.h` - Memory mapping
- `include/linux/vmalloc.h` - vmalloc family
- `include/linux/page.h` - Page operations
- `include/linux/folio.h` - Folio API (Linux 5.x+)

**Implementation:**
- `src/linux_slab.c` - kmem_cache, kmalloc
- `src/linux_page.c` - Page allocation, get_user_pages
- `src/linux_folio.c` - Folio operations
- `src/linux_shmemfs.c` - Shared memory filesystem

**Key Patterns:**
- Linux `kmalloc` → FreeBSD `malloc` with `M_LINUXKM`
- Linux `vmalloc` → FreeBSD `malloc` with `M_EXEC`
- Page operations use FreeBSD VM system

### Synchronization Primitives

**Headers:**
- `include/linux/spinlock.h` - Spinlocks
- `include/linux/mutex.h` - Mutexes
- `include/linux/rwsem.h` - Read-write semaphores
- `include/linux/ww_mutex.h` - Wound-wait mutexes
- `include/linux/wait.h` - Wait queues
- `include/linux/completion.h` - Completions

**Implementation:**
- `src/linux_lock.c` - Lock implementations
- `src/linux_work.c` - Workqueues
- `src/linux_wait.c` - Wait queues

**Key Patterns:**
- Spinlocks implemented using `mtx` (mutex) with spin semantics
- Mutexes use FreeBSD `struct mtx` or `struct sx`
- Wait queues use FreeBSD sleep/wakeup

### RCU (Read-Copy-Update)

**Headers:**
- `include/linux/rcupdate.h` - RCU API
- `include/linux/srcu.h` - Sleepable RCU

**Implementation:**
- `src/linux_rcu.c` - RCU implementation using `ck_epoch` (Concurrency Kit)

**Key Patterns:**
- Uses Concurrency Kit's epoch-based reclamation
- Supports both normal and sleepable RCU (SRCU)
- Grace periods tracked via ck_epoch mechanism

### Workqueues and Tasklets

**Headers:**
- `include/linux/workqueue.h` - Workqueues
- `include/linux/interrupt.h` - Tasklets

**Implementation:**
- `src/linux_work.c` - Workqueue implementation
- `src/linux_tasklet.c` - Tasklet implementation

**Key Patterns:**
- Workqueues implemented using FreeBSD `taskqueue`
- System workqueues: `system_wq`, `system_long_wq`, `system_highpri_wq`, etc.
- Tasklets run as high-priority tasks

### Timers

**Headers:**
- `include/linux/timer.h` - Standard timers
- `include/linux/hrtimer.h` - High-resolution timers

**Implementation:**
- `src/linux_hrtimer.c` - hrtimer implementation

**Key Patterns:**
- hrtimers implemented using `systimer` + `taskqueue`
- Supports `CLOCK_MONOTONIC`

### IRQ and Interrupts

**Headers:**
- `include/linux/interrupt.h` - IRQ handling
- `include/linux/irq_work.h` - IRQ work

**Implementation:**
- `src/linux_interrupt.c` - IRQ handling

### PCI Subsystem

**Headers:**
- `include/linux/pci.h` - PCI device API
- `include/linux/pcieport_if.h` - PCIe port services

**Implementation:**
- `src/linux_pci.c` - PCI glue layer

**Key Patterns:**
- Wraps FreeBSD PCI bus interface
- Maps Linux PCI config access to FreeBSD `pci_read_config`
- MSI/MSI-X support through FreeBSD bus interface

### USB Subsystem

**Headers:**
- `include/linux/usb.h` - USB device API
- `include/linux/usb/ch9.h` - USB chapter 9

**Implementation:**
- `src/linux_usb.c` - USB glue layer

### I2C Subsystem

**Headers:**
- `include/linux/i2c.h` - I2C API

**Implementation:**
- `src/linux_i2c.c` - I2C master/slave
- `src/linux_i2cbb.c` - I2C bit-banging
- Interface definition: `lkpi_iic_if.m`

### Networking

**Headers:**
- `include/linux/netdevice.h` - Network devices
- `include/linux/skbuff.h` - Socket buffers
- `include/net/cfg80211.h` - Wireless configuration
- `include/net/mac80211.h` - Wireless MAC

**Implementation:**
- `src/linux_netdev.c` - Network device glue
- `src/linux_skbuff.c` - sk_buff implementation
- `src/linux_80211.c` - 802.11/cfg80211/mac80211

### IDR/IDA (ID Allocation)

**Headers:**
- `include/linux/idr.h` - IDR API

**Implementation:**
- `src/linux_idr.c` - ID allocation using FreeBSD `idr` implementation

### DMA-BUF and Fences

**Headers:**
- `include/linux/dma-buf.h` - DMA buffer sharing
- `include/linux/dma-fence.h` - DMA fences

**Implementation:**
- DMA-BUF modeled as FreeBSD file handles
- Fence support for GPU synchronization

### Data Structures

**Headers:**
- `include/linux/list.h` - Linked lists
- `include/linux/rbtree.h` - Red-black trees
- `include/linux/radix-tree.h` - Radix trees
- `include/linux/xarray.h` - XArray (Linux 4.20+)
- `include/linux/hashtable.h` - Hash tables

**Implementation:**
- `src/linux_radix.c` - Radix tree implementation
- `src/linux_xarray.c` - XArray implementation

## Architecture Support

LinuxKPI supports:
- **amd64** (x86_64) - Full support
- **aarch64** (arm64) - Full support
- **i386** - Limited support, legacy

Architecture-specific code in `include/asm/`:
- `atomic.h` - Atomic operations
- `barrier.h` - Memory barriers
- `uaccess.h` - User access (copy_to_user, copy_from_user)
- `processor.h` - CPU features

## Naming Conventions

- **Public API**: Standard Linux names (e.g., `kmalloc`, `mutex_lock`)
- **LinuxKPI-specific**: `linuxkpi_*` prefix for module exports
- **Internal helpers**: `lkpi_*` prefix for internal functions

## Key Implementation Files Reference

| File | Purpose |
|------|---------|
| `linux_compat.c` | Core compatibility: ERR_PTR, container_of, etc. |
| `linux_current.c` | Current task and thread info |
| `linux_schedule.c` | Scheduling primitives |
| `linux_kthread.c` | Kernel thread management |
| `linux_slab.c` | Memory allocation |
| `linux_page.c` | Page allocation and manipulation |
| `linux_lock.c` | Locks, mutexes, semaphores |
| `linux_rcu.c` | RCU using ck_epoch |
| `linux_work.c` | Workqueues |
| `linux_pci.c` | PCI subsystem glue |
| `linux_usb.c` | USB subsystem glue |
| `linux_i2c.c` | I2C subsystem |
| `linux_netdev.c` | Network device glue |
| `linux_skbuff.c` | Socket buffers |
| `linux_80211.c` | 802.11 wireless |
| `linux_idr.c` | ID allocation |
| `linux_hrtimer.c` | High-resolution timers |
| `linux_tasklet.c` | Tasklets |
| `linux_kobject.c` | Kobject/kset/ktype |
| `linux_firmware.c` | Firmware loading |
| `linux_dma_buf.c` | DMA buffer sharing |
| `linux_fence.c` | DMA fences |

## Configuration

The main configuration header is `include/linux/kconfig.h`, which:
- Defines `CONFIG_*` macros expected by Linux drivers
- Sets architecture-specific config options
- Enables/disables features based on FreeBSD capabilities

## Porting Guidelines

When porting a Linux driver to FreeBSD using LinuxKPI:

1. **Add LinuxKPI includes** to your module Makefile
2. **Include headers** using Linux paths: `#include <linux/module.h>`
3. **Use Linux API** - avoid mixing FreeBSD and Linux primitives
4. **Check for unimplemented APIs** in `dummy/include/`
5. **Test thoroughly** - some APIs may be stubbed or behave differently

## Limitations and Stubbed APIs

Some Linux APIs are stubbed or partially implemented:
- Check `dummy/include/` for placeholder headers
- Some advanced features may panic with "not implemented"
- Performance characteristics may differ from Linux
- Not all sysfs/debugfs features are available

## References

- FreeBSD Handbook: "Linuxulator" chapter (for userland, but related concepts)
- Source tree: `sys/compat/linuxkpi/`
- Example consumers: `sys/dev/mlx4/`, `sys/dev/mlx5/`, `sys/dev/qat/`
