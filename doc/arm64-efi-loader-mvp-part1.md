# ARM64 EFI Loader MVP

## Overview

This document tracks the arm64 EFI loader and kernel bring-up for DragonFly BSD.

---

## MVP Part 1: EFI Loader Console Bring-up (COMPLETE)

### Goal

Produce an AArch64 UEFI loader binary that executes under QEMU+EDK2 and prints to the UEFI console.

### Status: COMPLETE

The arm64 EFI loader now:
- Loads as valid PE32+ EFI application
- Self-relocates correctly (fixed RELA relocation bug)
- Initializes EFI services (BS, RS, ST, IH)
- Allocates heap via EFI
- Initializes console - prints "Console: EFI console"
- Displays DragonFly banner and EFI firmware info
- Shows autoboot spinner countdown
- Drops to interactive `OK` prompt

### Key Fixes Applied

1. **RELA Relocation Bug** - `self_reloc.c` was double-adding the addend for RELA relocations. Fixed by assigning instead of adding: `*newaddr = baseaddr + rel->r_addend;`

2. **PE32+ Header** - Using embedded PE header in `start.S` (FreeBSD approach)

3. **Malloc Guard Issue** - Temporarily disabled guards in `stand/lib/zalloc_defs.h` to work around a `free()` crash. Root cause TBD.

### Files Modified

```
stand/boot/efi/loader/
├── arch/aarch64/
│   ├── start.S              # Entry + embedded PE header
│   ├── ldscript.aarch64     # Linker script for PE layout
│   └── elf64_freebsd.c      # Stub (returns EFTYPE)
├── self_reloc.c             # Fixed RELA relocation
├── efi_main.c               # EFI initialization
└── main.c                   # Loader main loop

stand/lib/
└── zalloc_defs.h            # Disabled guards temporarily
```

### Build Instructions (VM)

```sh
# Build libstand
cd /usr/src/stand/lib
make clean && make MACHINE_ARCH=aarch64 MACHINE=aarch64 CC=aarch64-none-elf-gcc

# Build loader
cd /usr/src/stand/boot/efi/loader
make clean && make MACHINE_ARCH=aarch64 MACHINE=aarch64 CC=aarch64-none-elf-gcc
```

### Test Instructions (Host)

See `tools/arm64-test/Makefile` for the test environment.

```sh
cd tools/arm64-test
make copy-loader    # Fetch loader from VM
make run-gui        # Run with graphical display
make test           # Run with 25s timeout (headless)
```

---

## MVP Part 2: Stub Kernel (PLANNED)

### Goal

Create a minimal arm64 kernel that the EFI loader can load and execute, prints a message to UART, and halts.

### Approach

Two components:
1. **Loader**: Implement `elf64_exec()` to load and jump to kernel
2. **Kernel**: Create stub that prints to PL011 UART (0x09000000 on QEMU virt)

### Part A: Loader Changes

**File:** `stand/boot/efi/loader/arch/aarch64/elf64_freebsd.c`

Implement `elf64_exec()` to:
1. Build module metadata (modulep)
2. Call `ExitBootServices()` to take control from UEFI
3. Flush caches (D-cache clean, I-cache invalidate)
4. Jump to kernel entry point with `modulep` in x0

### Part B: Kernel Platform Structure

Create `sys/platform/arm64/` with proper directory structure:

```
sys/platform/arm64/
├── Makefile.inc           # Platform build rules
├── conf/
│   └── files              # File list for kernel config
├── include/
│   └── (placeholder)      # Machine headers (later)
└── aarch64/
    ├── locore.S           # Entry point: _start
    ├── machdep.c          # Stub init functions
    └── Makefile           # Build the kernel
```

**Kernel entry (`locore.S`):**
1. Entry at `_start`, receive `modulep` in x0
2. Write "DragonFly arm64 kernel!\n" to PL011 UART at 0x09000000
3. Infinite loop (halt)

### Part C: Kernel Entry Signature

The loader passes a single argument to the kernel:

```c
void _start(vm_offset_t modulep);  // modulep in x0
```

The `modulep` pointer references preload metadata - a linked list of module information containing kernel address, size, environment, boot flags, EFI memory map, etc.

### Tasks

| # | Task | Files |
|---|------|-------|
| 1 | Implement `elf64_exec()` in loader | `stand/boot/efi/loader/arch/aarch64/elf64_freebsd.c` |
| 2 | Create platform directory structure | `sys/platform/arm64/` |
| 3 | Write `locore.S` stub entry point | `sys/platform/arm64/aarch64/locore.S` |
| 4 | Create minimal `machdep.c` | `sys/platform/arm64/aarch64/machdep.c` |
| 5 | Create Makefiles for kernel build | `sys/platform/arm64/Makefile.inc`, etc. |
| 6 | Update test Makefile | `tools/arm64-test/Makefile` |
| 7 | Build and test on VM | - |

### Success Criteria

- Loader successfully loads ELF64 kernel
- Loader exits boot services and jumps to kernel
- Kernel prints "DragonFly arm64 kernel!" to UART
- Message visible in QEMU serial output

---

## Development Environment

- **Local repo:** `/Users/tuxillo/s/dragonfly`
- **VM repo:** `/usr/src`
- **Branch:** `port-arm64`
- **VM access:** `ssh root@devbox.sector.int -p 6021`
- **Workflow:** commit locally, push to Gitea, pull on VM, build, copy back to test

---

## Debug Output Key (MVP Part 1)

These debug markers were added during bring-up:

**From start.S:** `1`=entry, `2`=BSS cleared, `3`=before reloc, `4`=after reloc, `5`=before efi_main

**From efi_main.c:** `A`=entered, `B`=got EFI ptrs, `C`=console ctrl, `D`=heap alloc, `E`=setheap, `F`=img protocol, `G`=args, `H`=calling main

**From main.c:** `a`=entered, `b`=archsw set, `c`=has_kbd done, `d`=cons_probe done, `e`=args parsed, `f`=efi_copy_init, `g`=devs probed, `h`=printing

---

*Last updated: 2026-01-22*
