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
# Cross-compiler on VM
CC=/usr/local/bin/aarch64-none-elf-gcc

# Build loader
cd /usr/src/stand/boot/efi/loader
make clean && make -j8 MACHINE_ARCH=aarch64 MACHINE=aarch64 CC=$CC

# Build kernel (via config(8))
cd /usr/src/sys/compile/ARM64_GENERIC
make clean && make -j8 kernel.debug
```

### Test Instructions (Host)

Use the build/test agent or manual workflow:

```sh
cd tools/arm64-test
make copy-all       # Fetch loader and kernel from VM
make test           # Run with 45s timeout (headless)
make run-gui        # Run with graphical display
```

---

## MVP Part 2: Stub Kernel (COMPLETE)

### Goal

Create a minimal arm64 kernel that the EFI loader can load and execute, prints a message to UART, and halts.

### Status: COMPLETE

The stub kernel milestone has been achieved and significantly exceeded. The kernel now:
- Loads via EFI loader with full module metadata
- Enters at `_start` with MMU enabled (identity-mapped)
- Parses boot metadata (modulep) including EFI memory map
- Runs `initarm()` C code successfully
- Prints diagnostic output to PL011 UART

### Key Implementation

**Loader (`stand/boot/efi/loader/arch/aarch64/elf64_freebsd.c`):**
- `elf64_exec()` builds module metadata via `bi_load()`
- Calls `ExitBootServices()` to take control from UEFI
- Flushes caches (D-cache clean, I-cache invalidate)
- Jumps to kernel entry with `modulep` in x0

**Kernel Platform Structure:**
```
sys/platform/arm64/
├── Makefile.inc           # Platform build rules
├── conf/
│   ├── ARM64_GENERIC      # Kernel config file
│   ├── Makefile           # Kernel build Makefile
│   ├── files              # File list for kernel config
│   ├── kern.mk            # Kernel build rules
│   └── ldscript.aarch64   # Linker script
├── include/               # Machine headers (machine/*.h)
└── aarch64/
    ├── locore.s           # Entry point with MMU setup
    ├── machdep.c          # initarm() and early C init
    ├── pmap.c             # Pmap stubs for linking
    ├── support.c          # Support routines (copy/fetch)
    └── genassym.c         # Assembly constants

sys/cpu/aarch64/include/   # CPU headers (cpu/*.h)
```

**Kernel entry flow:**
1. `_start` in `locore.s` receives `modulep` in x0
2. Sets up early identity map (TTBR0) for physical addresses
3. Enables MMU with 4KB pages
4. Calls `initarm(modulep)` in C

### Success Criteria (All Met)

- ✅ Loader successfully loads ELF64 kernel
- ✅ Loader exits boot services and jumps to kernel
- ✅ Kernel prints to UART (diagnostic output visible)
- ✅ Message visible in QEMU serial output

---

## MVP Part 3: Early MMU Bootstrap (IN PROGRESS)

### Goal

Replace the loader-side MMU disable workaround with a FreeBSD-derived, kernel-controlled page table setup that enables the MMU early and preserves the DragonFly boot flow.

### Status: IN PROGRESS (Phase C Complete)

**Completed:**

Phase A - Metadata consumption (modulep): ✅ COMPLETE
- `initarm()` receives modulep as physical address
- Parses module metadata to find kernel record
- Extracts: `boothowto`, `kern_envp`, `efi_systbl_phys`
- Prints diagnostic values over UART

Phase B - EFI memory map ingestion: ✅ COMPLETE
- Fetches EFI map header via `MODINFOMD_EFI_MAP`
- Validates descriptor size/version, counts 36 entries
- Builds physmem list: 8 usable ranges, ~112K pages (~440MB)
- Identifies largest contiguous range for bootstrap allocator

Phase C - Pmap bootstrap skeleton: ✅ COMPLETE
- Bootstrap allocator initialized from largest EFI range (0x48000000-0x5c13b000)
- L0/L1/L2 page tables allocated from bootstrap memory
- TTBR1 page tables built for kernel high-VA mapping
- Page table entries populated (2MB blocks for kernel text/data)

Phase C.5 - config(8) kernel build: ✅ COMPLETE
- `ARM64_GENERIC` kernel config works with `config -r -g`
- Full kernel build via `make kernel.debug` in `sys/compile/ARM64_GENERIC`
- Cross-compiler: `/usr/local/bin/aarch64-none-elf-gcc`
- Kernel links successfully with ~300 stub functions

**Current Boot Output:**
```
[arm64] initarm: modulep(phys)=0x0000000040caf000
[arm64] module: /boot/KERNEL/kernel
[arm64] kernend=0x0000000040cb0000
[arm64] boothowto=0x0000000000000000
[arm64] kern_envp=0x0000000040cae000
[arm64] efi_systbl=0x000000005ffd0018
[arm64] efi_map entries=0000000000000024
[arm64] efi_map usable_pages=000000000001b7f3
[arm64] physmem ranges=0000000000000008
[arm64] pmap bootstrap: largest range 0x0000000048000000-0x000000005c13b000
[arm64] boot_alloc range 0x0000000048000000-0x000000005c13b000
[arm64] pt l2=0x0000000048004000
[arm64] ttbr1 current=0x00000000406bd000
[arm64] ttbr1 candidate=0x0000000048002000
```

**Not done yet:**
- TTBR1 switch and high-VA trampoline execution
- Wire PL011 console backend into DragonFly console framework (Phase D)
- Replace ad-hoc UART prints with `kprintf()` via console framework

### Key Fixes Applied This Session

1. **Loader staging area too small** - Increased from 8MB to 32MB in `copy.c`
   - modulep was at ~12.7MB offset, beyond 8MB staging area
   - `efi_copyin()` was silently failing for modulep data
   - Fixed: staging now covers full kernel + metadata

2. **modulep physical vs virtual address** - Keep modulep as physical in early boot
   - Early page tables only map first 4MB at high VA
   - modulep at ~12MB needs TTBR0 identity map access
   - Fixed in `machdep.c`: don't convert to high VA prematurely

### Remaining Work (Phases C.6 and D)

Phase C.6: TTBR1 switch and high-VA execution
1. Switch TTBR1 from locore bootstrap tables to pmap-allocated tables
2. Execute high-VA trampoline to validate kernel mapping
3. Ensure UART remains accessible after switch (device mapping)
4. Continue boot in high-VA space

Phase D: Console framework handoff
1. Replace ad-hoc UART prints with proper early console hook
2. Enable `kprintf()` via console framework
3. Wire PL011 driver into DragonFly `CONS_DRIVER` framework

### Phase D Implementation Plan (PL011 Console Backend)

The PL011 console driver exists under `sys/dev/serial/pl011/`:
- `pl011_cons.c` - probe/init/putc implementation
- `pl011_reg.h` - register offsets and flags

Current state:
- Driver compiles and is included in kernel build
- Uses `CONS_DRIVER` macro for DragonFly console framework
- Early boot uses direct UART writes via `uart_putc()` in `machdep.c`

Remaining work:
- Call `cninit()` in early arm64 init path
- Replace `uart_put*` with `kprintf()` once `cnputc` is live
- Verify console works after TTBR1 switch (device mapping required)

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

#### 2) Kernel Entry Order (locore.s)

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

### FUTURE TODO

- Consider increasing arm64 EFI staging window to match FreeBSD defaults (64MB)
  - FreeBSD uses `DEFAULT_EFI_STAGING_SIZE` = 64MB for non-arm32
  - DragonFly currently uses 32MB fixed window for arm64 (increased from 8MB)
  - 32MB is sufficient for current kernel (~5MB) + metadata

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

*Last updated: 2026-01-26*
