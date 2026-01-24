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

## MVP Part 2: Stub Kernel (COMPLETE)

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
- Kernel prints "DragonFly/arm64 kernel started!" to UART
- Message visible in QEMU serial output

---

## MVP Part 3: Early MMU Bootstrap (PLANNED)

### Goal

Replace the loader-side MMU disable workaround with a FreeBSD-derived, kernel-controlled page table setup that enables the MMU early and preserves the DragonFly boot flow.

### Status: IN PROGRESS

Completed so far:
- Early TTBR0 identity map and MMU enable in `sys/platform/arm64/aarch64/locore.S`
- Early C entry (`initarm`) with modulep printing and minimal metadata parsing
- TTBR1 mapping split into RX text + RW/XN data (2MB blocks)
- High-VA trampoline to validate TTBR1 mapping
- Synchronous exception handler prints ESR/FAR/ELR over UART
- Loader allocates an executable window for the kernel (no MMU-disable handoff)
- Serial console auto-selection for headless runs (eficom)

Not done yet:
- Implement real early C init (boot flags/envp, EFI map, pmap bootstrap)
- Stabilize TTBR1 validation and idle loop (no stray output)

### Early C Init Plan (No FDT)

This is the immediate roadmap for the remaining MVP Part 3 work. The focus
is to consume loader metadata and bootstrap enough kernel state to proceed
without relying on ad-hoc UART prints or loader workarounds.

Phase A: Metadata consumption (modulep)
1. Set `preload_metadata` from `modulep` and relocate metadata pointers
   (`preload_bootstrap_relocate`) with the arm64 phys->virt offset.
2. Locate the kernel metadata record (`preload_search_by_type` for
   "elf kernel"/"elf64 kernel").
3. Fetch required metadata:
   - `boothowto = MD_FETCH(kmdp, MODINFOMD_HOWTO, int)`
   - `kern_envp = MD_FETCH(kmdp, MODINFOMD_ENVP, char *) + offset`
   - `efi_systbl_phys = MD_FETCH(kmdp, MODINFOMD_FW_HANDLE, vm_paddr_t)`
4. Print minimal diagnostics over UART to confirm the values.

Phase B: EFI memory map ingestion
1. Fetch EFI map header via `MODINFOMD_EFI_MAP`.
2. Validate descriptor size/version and count entries.
3. Build a minimal physmem list for bootstrap (no allocations yet).

Phase C: Pmap bootstrap skeleton
1. Introduce an arm64 pmap bootstrap entrypoint using the physmem list.
2. Transition from locore tables to pmap-managed tables (TTBR switch).
3. Keep UART working and avoid synchronous exceptions.

Phase D: Console framework handoff
1. Replace ad-hoc UART prints with a proper early console hook.
2. Enable `printf()` via the console framework as early as possible.

### FreeBSD Reference (Do Not Copy Without License)

Reference these files and follow their order of operations. Any reused code must include compatible FreeBSD license headers and be adapted to DragonFlyBSD subsystems and conventions.

**Loader:**
- `.freebsd.orig/stand/efi/loader/arch/arm64/exec.c`
- `.freebsd.orig/stand/efi/loader/arch/arm64/start.S`

**Kernel entry/MMU:**
- `.freebsd.orig/sys/arm64/arm64/locore.S`
- `.freebsd.orig/sys/arm64/arm64/machdep.c`
- `.freebsd.orig/sys/arm64/arm64/machdep_boot.c`
- `.freebsd.orig/sys/arm64/include/machdep.h`
- `.freebsd.orig/sys/arm64/arm64/pmap.c`

### Plan (Clear Steps by Section)

#### 1) Loader Handoff (No New Work Required)

1. Keep the existing DragonFly loader flow: `dev_cleanup()`, `bi_load()`, cache flush, jump to kernel entry with `modulep` in x0.
2. Once kernel MMU setup is implemented, remove the loader-side MMU disable workaround.

#### 2) Kernel Entry Order (locore.S)

1. Enter the kernel exception level (EL1) and ensure MMU is off in EL2 entry cases.
2. Compute the physical load address (FreeBSD uses `get_load_phys_addr`).
3. Build page tables (see section 3).
4. Enable MMU (see section 4).
5. Switch to the virtual address space.
6. Set up an initial stack, zero BSS.
7. Build a bootparams struct containing at least: modulep, kern_stack, kern_ttbr0, boot_el.
8. Branch to early C init (DragonFly equivalent of `initarm`).

#### 3) Early Page Tables (Identity + Kernel + UART)

1. Create TTBR1 mappings for the kernel virtual region:
   - Text: read-only, executable
   - Data/BSS: read-write, XN
2. Create a TTBR0 bootstrap identity map for the kernel load address (VA=PA).
3. Map UART/PL011 device region early (device memory type) so early prints keep working.
4. Keep the layout minimal: only the kernel and UART mappings needed to reach C init.

#### 4) MMU Enable Sequence

1. Set exception vectors (VBAR_EL1).
2. Load TTBR0/TTBR1 with bootstrap + kernel tables.
3. Invalidate TLBs and synchronize (DSB/ISB).
4. Program MAIR_EL1 with device and normal WB attributes.
5. Program TCR_EL1 for 4K pages, inner/outer WBWA, shareability.
6. Enable SCTLR_EL1 MMU + caches and issue ISB.

#### 5) Early C Init (DragonFly-side)

1. Parse boot metadata from `modulep` (FreeBSD uses `parse_boot_param`).
2. Initialize per-CPU data needed by pmap bootstrap.
3. Bootstrap DMAP (or DragonFly equivalent) and exclude EFI ranges as needed.
4. Complete pmap bootstrap.
5. Initialize console (`cninit`) and switch to final TTBR0 if required.

#### 6) Tests

1. Rebuild loader and kernel on the VM.
2. Copy artifacts with `tools/arm64-test/Makefile` targets.
3. Run `make test TEST_TIMEOUT=300`.
4. Expect UART output from the kernel without needing to disable MMU in the loader.

### Success Criteria

- Kernel boots with MMU enabled by the kernel (no loader-side MMU disable).
- Early UART output works after enabling MMU.
- No instruction aborts on kernel entry.
- Boot metadata is readable in C init.

### Appendix: Serial Console Cleanup Plan (FreeBSD Strategy)

Goal: Headless runs (`make run`/`make test`) produce clean serial output with
no EFI cursor spam or UTF-16 garbage by selecting the EFI serial console
(`eficom`) automatically, while GUI runs keep using the EFI text console.

Constraints observed:
- `startup.nsh` is not guaranteed to run under EDK2 Boot#### flow, so console
  selection must not rely on argv alone.
- The garbage output is generated by EFI text console terminal emulation
  (`efi_console` with TERM_EMU) calling `SetCursorPosition()` and by direct
  `ConOut->OutputString()` UTF-16 output on serial.

Exact plan (do not skip any step):

1) Confirm current behavior (no code change)
- Verify headless output shows `Console: EFI console` and cursor spam.
- Verify GUI output shows EFI console text in the QEMU window.

2) Fix `eficom` probe semantics (required prerequisite)
- `cons_probe()` clears console flags before probing; `eficom` must be able
  to set `C_PRESENTIN|C_PRESENTOUT` during probe without requiring
  `C_ACTIVEIN|C_ACTIVEOUT`.
- Keep any “active-only” hardware configuration in `comc_init()` if needed.

3) Implement FreeBSD-style auto console selection (ConOut/ConIn device paths)
- Port minimal logic of FreeBSD `parse_uefi_con_out()` into DragonFly
  (read UEFI variables `ConOut`, `ConOutDev`, `ConIn`, parse device paths
  for serial/video).
- Before `cons_probe()` in the EFI loader, set `console=eficom` when serial
  is preferred, else `console=efi` for GUI.
- Do not rely on `startup.nsh` arguments for selection.

4) Gate direct EFI text output when serial console is active
- Avoid `ST->ConOut->OutputString()` unless the active console includes `efi`.
- Ensure the firmware vendor line prints ASCII-only on serial.

5) Align test harness with the console strategy
- Headless `run`/`test`: rely on auto-selection, keep serial on stdio.
- GUI `run-gui`: keep EFI text console in window; serial remains for kernel UART.
- Remove any dependency on `console=` argv in `startup.nsh` for headless tests.

6) Validate
- Headless output shows `Console: serial port` (or equivalent) and no cursor spam.
- `EFI Firmware:` line has no UTF-16 garbage.
- Kernel UART output remains visible.

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
